using DynamoGUI.Models;
using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace DynamoGUI.Services;

public sealed class IpcClient : IDisposable
{
    private readonly object _connectionLock = new();
    private readonly object _pendingLock = new();
    private readonly SemaphoreSlim _sendLock = new(1, 1);
    private readonly Dictionary<int, TaskCompletionSource<BackendCommandResult>> _pendingCommands = [];
    private NamedPipeClientStream? _pipe;
    private CancellationTokenSource? _cts;
    private Task? _readTask;
    private int _disconnectSignaled;
    private int _nextRequestId;
    private bool _disposed;
    private string _pipeName = "DYNAMO_IPC";

    private static readonly System.Text.Json.JsonSerializerOptions JsonOptions = JsonDefaults.Options;

    public enum MessageType : byte
    {
        StatusSnapshot = 0x01,
        ProfilesSnapshot = 0x02,
        LogLine = 0x03,
        ProfileDocument = 0x04,
        CommandResult = 0x05,
        StartBot = 0x80,
        StopBot = 0x81,
        LoadProfile = 0x82,
        SaveProfile = 0x83,
        RequestShutdown = 0x84,
        GetStatus = 0x85,
        GetProfiles = 0x86,
        ConnectGame = 0x87,
        DisconnectGame = 0x88,
        GetActiveProfile = 0x89,
        SaveProfileDocument = 0x8A,
        MoveTo = 0x8B,
        PauseBot = 0x8C,
    }

    public event Action? OnConnected;
    public event Action? OnDisconnected;
    public event Action<string>? OnError;
    public event Action<string>? OnLogLine;
    public event Action<BackendStatusSnapshot>? OnStatusSnapshot;
    public event Action<ProfileListSnapshot>? OnProfilesSnapshot;
    public event Action<BotProfile>? OnProfileDocument;
    public event Action<BackendCommandResult>? OnCommandResult;

    public BackendStatusSnapshot? LastStatusSnapshot { get; private set; }

    public bool IsConnected
    {
        get
        {
            lock (_connectionLock)
            {
                return _pipe?.IsConnected ?? false;
            }
        }
    }

    public string PipeName => _pipeName;

    public async Task<bool> ConnectAsync(string pipeName = "DYNAMO_IPC", int timeoutMs = 5000)
    {
        if (_disposed)
        {
            OnError?.Invoke("IPC client is disposed.");
            return false;
        }

        NamedPipeClientStream? newPipe = null;
        try
        {
            _pipeName = string.IsNullOrWhiteSpace(pipeName) ? "DYNAMO_IPC" : pipeName.Trim();
            newPipe = new NamedPipeClientStream(".", _pipeName, PipeDirection.InOut, PipeOptions.Asynchronous);

            using var cts = new CancellationTokenSource(timeoutMs);
            await newPipe.ConnectAsync(cts.Token).ConfigureAwait(false);

            var readCts = new CancellationTokenSource();
            var connectedPipe = newPipe;
            var readTask = Task.Run(() => ReadLoopAsync(connectedPipe, readCts.Token));

            ResetDisconnectedSignal();
            ReplaceConnection(newPipe, readCts, readTask);
            newPipe = null;

            OnConnected?.Invoke();
            return true;
        }
        catch (Exception ex)
        {
            OnError?.Invoke($"IPC connect failed: {ex.Message}");
            return false;
        }
        finally
        {
            newPipe?.Dispose();
        }
    }

    public void Disconnect()
    {
        NamedPipeClientStream? pipe;
        CancellationTokenSource? cts;
        bool hadConnection;

        lock (_connectionLock)
        {
            pipe = _pipe;
            cts = _cts;
            hadConnection = pipe != null;
            _pipe = null;
            _cts = null;
            _readTask = null;
        }

        try
        {
            cts?.Cancel();
        }
        catch
        {
        }

        pipe?.Dispose();
        cts?.Dispose();
        FailPendingCommands("IPC disconnected.");

        if (hadConnection)
        {
            LastStatusSnapshot = null;
            SignalDisconnected();
        }
    }

    private void ReplaceConnection(NamedPipeClientStream newPipe, CancellationTokenSource newCts, Task newReadTask)
    {
        NamedPipeClientStream? oldPipe;
        CancellationTokenSource? oldCts;

        lock (_connectionLock)
        {
            oldPipe = _pipe;
            oldCts = _cts;
            _pipe = newPipe;
            _cts = newCts;
            _readTask = newReadTask;
        }

        try
        {
            oldCts?.Cancel();
        }
        catch
        {
        }

        oldPipe?.Dispose();
        oldCts?.Dispose();
    }

    private async Task ReadLoopAsync(NamedPipeClientStream pipe, CancellationToken ct)
    {
        var lengthBuffer = new byte[4];

        try
        {
            while (!ct.IsCancellationRequested && pipe.IsConnected)
            {
                var read = await ReadExactAsync(pipe, lengthBuffer, 0, 4, ct).ConfigureAwait(false);
                if (read < 4) break;

                var bodyLength = BinaryPrimitives.ReadInt32LittleEndian(lengthBuffer);
                if (bodyLength <= 0 || bodyLength > 1024 * 1024) break;

                var body = new byte[bodyLength];
                read = await ReadExactAsync(pipe, body, 0, bodyLength, ct).ConfigureAwait(false);
                if (read < bodyLength) break;

                ProcessMessage((MessageType)body[0], body.AsSpan(1).ToArray());
            }
        }
        catch (OperationCanceledException)
        {
        }
        catch (Exception ex)
        {
            OnError?.Invoke($"IPC read failed: {ex.Message}");
        }
        finally
        {
            CancellationTokenSource? ctsToDispose = null;
            var notify = false;

            lock (_connectionLock)
            {
                if (ReferenceEquals(_pipe, pipe))
                {
                    _pipe = null;
                    ctsToDispose = _cts;
                    _cts = null;
                    _readTask = null;
                    notify = true;
                }
            }

            pipe.Dispose();
            ctsToDispose?.Dispose();
            FailPendingCommands("IPC disconnected.");
            if (notify)
            {
                LastStatusSnapshot = null;
                SignalDisconnected();
            }
        }
    }

    private void ProcessMessage(MessageType type, byte[] payload)
    {
        try
        {
            switch (type)
            {
                case MessageType.StatusSnapshot:
                    var status = JsonSerializer.Deserialize<BackendStatusSnapshot>(payload, JsonOptions);
                    if (status != null)
                    {
                        LastStatusSnapshot = status;
                        OnStatusSnapshot?.Invoke(status);
                    }
                    break;
                case MessageType.ProfilesSnapshot:
                    var profiles = JsonSerializer.Deserialize<ProfileListSnapshot>(payload, JsonOptions);
                    if (profiles != null) OnProfilesSnapshot?.Invoke(profiles);
                    break;
                case MessageType.ProfileDocument:
                    var profile = JsonSerializer.Deserialize<BotProfile>(payload, JsonOptions);
                    if (profile != null) OnProfileDocument?.Invoke(profile);
                    break;
                case MessageType.CommandResult:
                    var result = JsonSerializer.Deserialize<BackendCommandResult>(payload, JsonOptions);
                    if (result != null)
                    {
                        CompletePendingCommand(result);
                        OnCommandResult?.Invoke(result);
                    }
                    break;
                case MessageType.LogLine:
                    OnLogLine?.Invoke(Encoding.UTF8.GetString(payload));
                    break;
            }
        }
        catch (Exception ex)
        {
            OnError?.Invoke($"IPC parse failed: {ex.Message}");
        }
    }

    private static async Task<int> ReadExactAsync(
        NamedPipeClientStream pipe,
        byte[] buffer,
        int offset,
        int count,
        CancellationToken ct)
    {
        var totalRead = 0;
        while (totalRead < count && !ct.IsCancellationRequested && pipe.IsConnected)
        {
            var read = await pipe.ReadAsync(buffer.AsMemory(offset + totalRead, count - totalRead), ct).ConfigureAwait(false);
            if (read == 0) break;
            totalRead += read;
        }
        return totalRead;
    }

    private async Task<bool> SendMessageAsync(MessageType type, int requestId, byte[] payload)
    {
        NamedPipeClientStream? pipe;
        lock (_connectionLock)
        {
            pipe = _pipe;
        }

        if (pipe == null || !pipe.IsConnected)
        {
            return false;
        }

        await _sendLock.WaitAsync().ConfigureAwait(false);
        try
        {
            if (!pipe.IsConnected) return false;

            var bodyLength = 1 + sizeof(int) + payload.Length;
            var buffer = new byte[4 + bodyLength];
            BinaryPrimitives.WriteInt32LittleEndian(buffer, bodyLength);
            buffer[4] = (byte)type;
            BinaryPrimitives.WriteInt32LittleEndian(buffer.AsSpan(5, sizeof(int)), requestId);
            payload.CopyTo(buffer, 9);

            await pipe.WriteAsync(buffer).ConfigureAwait(false);
            await pipe.FlushAsync().ConfigureAwait(false);
            return true;
        }
        catch (Exception ex)
        {
            OnError?.Invoke($"IPC write failed: {ex.Message}");
            Disconnect();
            return false;
        }
        finally
        {
            _sendLock.Release();
        }
    }

    private async Task<BackendCommandResult> SendCommandAsync(
        MessageType type,
        byte[] payload,
        int timeoutMs = 5000)
    {
        if (!IsConnected)
        {
            return CreateFailureResult(0, type, "IPC is not connected.");
        }

        var requestId = Interlocked.Increment(ref _nextRequestId);
        var pending = new TaskCompletionSource<BackendCommandResult>(
            TaskCreationOptions.RunContinuationsAsynchronously);

        lock (_pendingLock)
        {
            _pendingCommands[requestId] = pending;
        }

        var sent = await SendMessageAsync(type, requestId, payload).ConfigureAwait(false);
        if (!sent)
        {
            RemovePendingCommand(requestId);
            return CreateFailureResult(requestId, type, "IPC write failed.");
        }

        var completed = await Task.WhenAny(pending.Task, Task.Delay(timeoutMs)).ConfigureAwait(false);
        if (completed == pending.Task)
        {
            return await pending.Task.ConfigureAwait(false);
        }

        RemovePendingCommand(requestId);
        return CreateFailureResult(requestId, type, "Command timed out.");
    }

    private async Task SendNotificationAsync(MessageType type, byte[] payload)
    {
        _ = await SendMessageAsync(type, 0, payload).ConfigureAwait(false);
    }

    public Task RequestStatusAsync() => SendNotificationAsync(MessageType.GetStatus, []);
    public Task RequestProfilesAsync() => SendNotificationAsync(MessageType.GetProfiles, []);
    public Task RequestActiveProfileAsync() => SendNotificationAsync(MessageType.GetActiveProfile, []);
    public Task StartBotAsync() => SendNotificationAsync(MessageType.StartBot, []);
    public Task StopBotAsync() => SendNotificationAsync(MessageType.StopBot, []);
    public Task PauseBotAsync() => SendNotificationAsync(MessageType.PauseBot, []);
    public Task SaveProfileAsync() => SendNotificationAsync(MessageType.SaveProfile, []);
    public Task DisconnectGameAsync() => SendNotificationAsync(MessageType.DisconnectGame, []);

    public Task MoveToAsync(int gameX, int gameY)
    {
        var payload = JsonSerializer.SerializeToUtf8Bytes(new { x = gameX, y = gameY }, JsonOptions);
        return SendNotificationAsync(MessageType.MoveTo, payload);
    }

    public Task<BackendCommandResult> RequestShutdownAsync() =>
        SendCommandAsync(MessageType.RequestShutdown, []);

    public Task<BackendCommandResult> LoadProfileAsync(string profileName) =>
        SendCommandAsync(MessageType.LoadProfile, Encoding.UTF8.GetBytes(profileName ?? string.Empty));

    public Task<BackendCommandResult> SaveProfileDocumentAsync(BotProfile profile)
    {
        profile ??= new BotProfile();
        var payload = JsonSerializer.SerializeToUtf8Bytes(profile, JsonOptions);
        return SendCommandAsync(MessageType.SaveProfileDocument, payload);
    }

    public Task<BackendCommandResult> ConnectGameAsync(BackendConnectRequest request)
    {
        request ??= new BackendConnectRequest();
        var payload = JsonSerializer.SerializeToUtf8Bytes(request, JsonOptions);
        return SendCommandAsync(MessageType.ConnectGame, payload, 20000);
    }

    private static BackendCommandResult CreateFailureResult(int requestId, MessageType type, string message) =>
        new()
        {
            RequestId = requestId,
            CommandType = (int)type,
            Success = false,
            Message = message,
        };

    private void CompletePendingCommand(BackendCommandResult result)
    {
        TaskCompletionSource<BackendCommandResult>? pending = null;
        lock (_pendingLock)
        {
            if (_pendingCommands.TryGetValue(result.RequestId, out pending))
            {
                _pendingCommands.Remove(result.RequestId);
            }
        }

        pending?.TrySetResult(result);
    }

    private void RemovePendingCommand(int requestId)
    {
        lock (_pendingLock)
        {
            _pendingCommands.Remove(requestId);
        }
    }

    private void FailPendingCommands(string message)
    {
        List<TaskCompletionSource<BackendCommandResult>> pending;
        lock (_pendingLock)
        {
            if (_pendingCommands.Count == 0)
            {
                return;
            }

            pending = [.. _pendingCommands.Values];
            _pendingCommands.Clear();
        }

        foreach (var item in pending)
        {
            item.TrySetResult(new BackendCommandResult
            {
                Success = false,
                Message = message,
            });
        }
    }

    private void ResetDisconnectedSignal() => Interlocked.Exchange(ref _disconnectSignaled, 0);

    private void SignalDisconnected()
    {
        if (Interlocked.Exchange(ref _disconnectSignaled, 1) == 0)
        {
            OnDisconnected?.Invoke();
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        Disconnect();
        _sendLock.Dispose();
    }
}
