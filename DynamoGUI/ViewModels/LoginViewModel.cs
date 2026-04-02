using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Avalonia.Threading;
using DynamoGUI.Models;
using DynamoGUI.Services;
using System;
using System.Collections.ObjectModel;
using System.Threading.Tasks;

namespace DynamoGUI.ViewModels;

public partial class LoginViewModel : ViewModelBase
{
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(LoginCommand))]
    private string _username = string.Empty;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(LoginCommand))]
    private string _password = string.Empty;

    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(LoginCommand))]
    private ServerEntry? _selectedServer;

    [ObservableProperty]
    private bool _isBusy;

    [ObservableProperty]
    private string? _errorMessage;

    [ObservableProperty]
    private string _statusText = string.Empty;

    [ObservableProperty]
    private bool _isLoadingServers = true;

    public ObservableCollection<ServerEntry> Servers { get; } = [];

    public AppSession? Session { get; private set; }

    public event Action? LoginSuccessful;

    public LoginViewModel()
    {
        _ = LoadServersAsync();
    }

    private static void Trace(string message)
    {
        _ = message;
    }

    private bool CanLogin() =>
        !IsBusy
        && !string.IsNullOrWhiteSpace(Username)
        && !string.IsNullOrWhiteSpace(Password)
        && SelectedServer is not null;

    [RelayCommand(CanExecute = nameof(CanLogin))]
    private async Task LoginAsync()
    {
        IsBusy = true;
        ErrorMessage = null;
        Trace("----- LOGIN ATTEMPT -----");
        IpcClient? ipc = null;
        BackendService? backend = null;
        Action<BackendStatusSnapshot>? statusListener = null;
        Action<string>? ipcErrorListener = null;
        Action? ipcDisconnectedListener = null;
        Action<string>? backendErrorListener = null;
        Action<int>? backendExitedListener = null;
        var reachedLiveState = new TaskCompletionSource<BackendStatusSnapshot>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var failedLogin = new TaskCompletionSource<string>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        BackendStatusSnapshot? lastObservedStatus = null;
        var connectAccepted = false;
        var observedConnectionLifecycle = false;

        try
        {
            StatusText = "Locating backend...";
            var backendPath = BackendService.FindBackendPath();
            Trace($"Backend path: {backendPath ?? "<null>"}");
            if (backendPath is null)
            {
                ErrorMessage = "Backend payload is not available.";
                Trace($"Login failed: {ErrorMessage}");
                return;
            }

            Session = new AppSession();
            backend = Session.Backend;
            var pipeName = BackendService.CreateSessionPipeName();
            Trace($"Pipe name: {pipeName}");

            StatusText = "Starting backend...";
            var started = await backend.StartAsync(backendPath, ["--pipe", pipeName]);
            Trace($"Backend started: {started}");
            if (!started)
            {
                ErrorMessage = "Failed to start backend process.";
                Trace($"Login failed: {ErrorMessage}");
                Session.Dispose();
                Session = null;
                return;
            }

            StatusText = "Connecting to backend...";
            var connected = await Session.Ipc.ConnectAsync(pipeName, 5000);
            Trace($"IPC connected: {connected}");
            if (!connected)
            {
                ErrorMessage = "Could not connect to backend IPC pipe.";
                Trace($"Login failed: {ErrorMessage}");
                Session.Dispose();
                Session = null;
                return;
            }
            ipc = Session.Ipc;

            StatusText = "Logging in...";
            var request = new BackendConnectRequest
            {
                Username = Username.Trim(),
                Password = Password,
                ServerId = SelectedServer!.Id,
                Language = "en",
            };

            statusListener = status =>
            {
                lastObservedStatus = status;
                if (status.EngineState is "Connecting" or "Authenticating" or "Loading" or "InGame")
                {
                    observedConnectionLifecycle = true;
                }
                Trace($"Status: engine={status.EngineState}, connection={status.ConnectionState}, error={status.EngineError}");
                Dispatcher.UIThread.Post(() =>
                {
                    StatusText = status.EngineState switch
                    {
                        "Connecting" => "Connecting to game services...",
                        "Authenticating" => "Authenticating session...",
                        "Loading" => "Loading game state...",
                        "InGame" => "Session ready.",
                        "Disconnected" => "Session disconnected.",
                        "Error" => "Backend reported an error.",
                        _ => "Waiting for engine..."
                    };
                });
                if (status.EngineState == "InGame")
                {
                    reachedLiveState.TrySetResult(status);
                    return;
                }

                if (!connectAccepted)
                {
                    return;
                }

                if (status.EngineState == "Error" ||
                    (status.EngineState == "Disconnected" && observedConnectionLifecycle))
                {
                    failedLogin.TrySetResult(
                        string.IsNullOrWhiteSpace(status.EngineError)
                            ? $"Backend reported {status.EngineState}."
                            : status.EngineError
                    );
                }
            };
            ipcErrorListener = message =>
            {
                Trace($"IPC error: {message}");
                failedLogin.TrySetResult(message);
            };
            ipcDisconnectedListener = () =>
            {
                Trace("IPC disconnected during login.");
                failedLogin.TrySetResult("IPC disconnected during login.");
            };
            backendErrorListener = message =>
            {
                Trace($"Backend error: {message}");
                failedLogin.TrySetResult(message);
            };
            backend.OnOutput += Trace;
            backendExitedListener = exitCode =>
            {
                Trace($"Backend exited with code {exitCode}.");
                failedLogin.TrySetResult($"Backend exited with code {exitCode}.");
            };

            ipc.OnStatusSnapshot += statusListener;
            ipc.OnError += ipcErrorListener;
            ipc.OnDisconnected += ipcDisconnectedListener;
            backend.OnError += backendErrorListener;
            backend.OnExited += backendExitedListener;

            var connectResult = await ipc.ConnectGameAsync(request);
            Trace($"ConnectGame result: success={connectResult.Success}, message={connectResult.Message}");
            if (!connectResult.Success)
            {
                ErrorMessage = string.IsNullOrWhiteSpace(connectResult.Message)
                    ? "Backend rejected the login request."
                    : connectResult.Message;
                Trace($"Login failed: {ErrorMessage}");
                Session.Dispose();
                Session = null;
                return;
            }

            connectAccepted = true;
            Session.LastConnectRequest = new BackendConnectRequest
            {
                Username = request.Username,
                Password = request.Password,
                ServerId = request.ServerId,
                Language = request.Language,
            };
            StatusText = "Waiting for engine...";
            var completed = await Task.WhenAny(
                reachedLiveState.Task,
                failedLogin.Task,
                Task.Delay(TimeSpan.FromSeconds(45))
            );

            if (completed == reachedLiveState.Task)
            {
                StatusText = "Session ready.";
                Trace("Login successful.");
                LoginSuccessful?.Invoke();
                return;
            }

            if (completed == failedLogin.Task)
            {
                ErrorMessage = await failedLogin.Task;
                Trace($"Login failed: {ErrorMessage}");
            }
            else
            {
                var finalStatus = lastObservedStatus ?? ipc.LastStatusSnapshot;
                ErrorMessage = finalStatus is not null && !string.IsNullOrWhiteSpace(finalStatus.EngineError)
                    ? finalStatus.EngineError
                    : "Backend did not reach InGame state in time.";
                Trace($"Login failed: {ErrorMessage}");
            }

            Session.Dispose();
            Session = null;
        }
        catch (Exception ex)
        {
            ErrorMessage = $"Login failed: {ex.Message}";
            Trace($"Login exception: {ex}");
            Session?.Dispose();
            Session = null;
        }
        finally
        {
            if (ipc is not null && statusListener is not null)
            {
                ipc.OnStatusSnapshot -= statusListener;
            }
            if (ipc is not null && ipcErrorListener is not null)
            {
                ipc.OnError -= ipcErrorListener;
            }
            if (ipc is not null && ipcDisconnectedListener is not null)
            {
                ipc.OnDisconnected -= ipcDisconnectedListener;
            }
            if (backend is not null && backendErrorListener is not null)
            {
                backend.OnError -= backendErrorListener;
            }
            if (backend is not null)
            {
                backend.OnOutput -= Trace;
            }
            if (backend is not null && backendExitedListener is not null)
            {
                backend.OnExited -= backendExitedListener;
            }
            IsBusy = false;
            if (ErrorMessage is not null) StatusText = string.Empty;
        }
    }

    private async Task LoadServersAsync()
    {
        IsLoadingServers = true;
        try
        {
            var servers = await WarUniverseApiClient.FetchServersAsync();
            foreach (var s in servers)
                Servers.Add(s);

            if (Servers.Count > 0)
                SelectedServer = Servers[0];
        }
        finally
        {
            IsLoadingServers = false;
        }
    }

    partial void OnIsBusyChanged(bool value) => LoginCommand.NotifyCanExecuteChanged();
}
