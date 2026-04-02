using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace DynamoGUI.Services;

public sealed class BackendService : IDisposable
{
    public const string BackendExecutableName = "DynamoAPI.exe";
    private const int MaxBufferedCrashLines = 200;

    private Process? _process;
    private bool _disposed;
    private bool _stopRequested;
    private string? _lastExecutablePath;
    private IReadOnlyList<string> _lastArguments = Array.Empty<string>();
    private readonly object _crashLogLock = new();
    private readonly Queue<string> _recentProcessLines = new();

    public event Action<string>? OnOutput;
    public event Action<string>? OnError;
    public event Action<int>? OnExited;

    public bool IsRunning => _process is { HasExited: false };
    public bool CanRestartLastProcess => !string.IsNullOrWhiteSpace(_lastExecutablePath);
    public static string CrashLogPath => Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "crash_log.txt");

    public static string CreateSessionPipeName() => $"DYNAMO_IPC_{Guid.NewGuid():N}";

    public static string? FindBackendPath()
    {
        var exeDir = AppDomain.CurrentDomain.BaseDirectory;
        string[] candidates =
        [
            Path.Combine(exeDir, "backend", BackendExecutableName),
            Path.Combine(exeDir, BackendExecutableName),
            Path.Combine(exeDir, "..", "..", "..", "..", "DynamoBOT", "build", "Release", BackendExecutableName),
            Path.Combine(exeDir, "..", "..", "..", "..", "DynamoBOT", "build", "Release", "dynamo_backend_host.exe"),
            Path.Combine(exeDir, "..", "..", "..", "..", "..", "DynamoBOT", "build", "Release", BackendExecutableName),
            Path.Combine(exeDir, "..", "..", "..", "..", "..", "DynamoBOT", "build", "Release", "dynamo_backend_host.exe"),
            Path.Combine(exeDir, "..", "..", "..", "..", "..", "..", "DynamoBOT", "build", "Release", BackendExecutableName),
            Path.Combine(exeDir, "..", "..", "..", "..", "..", "..", "DynamoBOT", "build", "Release", "dynamo_backend_host.exe"),
        ];

        foreach (var candidate in candidates)
        {
            var fullPath = Path.GetFullPath(candidate);
            if (File.Exists(fullPath))
            {
                return fullPath;
            }
        }

        return null;
    }

    private static bool ContainsProfilesDirectory(string root)
    {
        if (string.IsNullOrWhiteSpace(root))
        {
            return false;
        }

        var fullRoot = Path.GetFullPath(root);
        return Directory.Exists(Path.Combine(fullRoot, "config", "profiles")) ||
               Directory.Exists(Path.Combine(fullRoot, "DynamoBOT", "config", "profiles"));
    }

    private static string ResolveWorkingDirectory(string executablePath)
    {
        var appBase = AppDomain.CurrentDomain.BaseDirectory;
        if (ContainsProfilesDirectory(appBase))
        {
            return appBase;
        }

        var currentDirectory = Environment.CurrentDirectory;
        if (ContainsProfilesDirectory(currentDirectory))
        {
            return currentDirectory;
        }

        return Path.GetDirectoryName(executablePath) ?? currentDirectory;
    }

    public async Task<bool> StartAsync(string executablePath, IEnumerable<string>? arguments = null)
    {
        if (string.IsNullOrWhiteSpace(executablePath) || !File.Exists(executablePath))
        {
            OnError?.Invoke("Backend executable not found.");
            return false;
        }

        if (IsRunning)
        {
            OnOutput?.Invoke("Backend is already running.");
            return true;
        }

        try
        {
            var argumentList = (arguments ?? Enumerable.Empty<string>()).ToArray();
            var startInfo = new ProcessStartInfo
            {
                FileName = executablePath,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
                WorkingDirectory = ResolveWorkingDirectory(executablePath),
            };

            foreach (var argument in argumentList)
            {
                startInfo.ArgumentList.Add(argument);
            }

            _stopRequested = false;
            _lastExecutablePath = executablePath;
            _lastArguments = argumentList;
            lock (_crashLogLock)
            {
                _recentProcessLines.Clear();
            }

            var process = new Process { StartInfo = startInfo, EnableRaisingEvents = true };
            process.OutputDataReceived += (_, e) =>
            {
                if (string.IsNullOrWhiteSpace(e.Data))
                {
                    return;
                }

                BufferCrashLine("OUT", e.Data);
                OnOutput?.Invoke(e.Data);
            };
            process.ErrorDataReceived += (_, e) =>
            {
                if (string.IsNullOrWhiteSpace(e.Data))
                {
                    return;
                }

                BufferCrashLine("ERR", e.Data);
                OnError?.Invoke(e.Data);
            };
            process.Exited += (_, _) =>
            {
                var exitCode = process.ExitCode;
                if (!_stopRequested)
                {
                    WriteCrashLog(process, exitCode);
                }
                OnExited?.Invoke(exitCode);
            };

            _process = process;
            process.Start();
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();

            await Task.Delay(400);
            if (_process.HasExited)
            {
                OnError?.Invoke($"Backend exited with code {_process.ExitCode}.");
                return false;
            }

            return true;
        }
        catch (Exception ex)
        {
            WriteCrashLogMessage($"Failed to start backend: {ex}");
            OnError?.Invoke($"Failed to start backend: {ex.Message}");
            return false;
        }
    }

    public Task<bool> RestartLastAsync()
    {
        if (string.IsNullOrWhiteSpace(_lastExecutablePath))
        {
            OnError?.Invoke("No previous backend launch configuration is available.");
            return Task.FromResult(false);
        }

        return StartAsync(_lastExecutablePath, _lastArguments);
    }

    public void Stop()
    {
        if (_process is null || _process.HasExited)
        {
            return;
        }

        try
        {
            _stopRequested = true;
            _process.Kill(entireProcessTree: true);
            _process.WaitForExit(3000);
        }
        catch
        {
            // Keep stop path best-effort.
        }

        _process.Dispose();
        _process = null;
    }

    private void BufferCrashLine(string source, string message)
    {
        var line = $"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} [{source}] {message}";
        lock (_crashLogLock)
        {
            _recentProcessLines.Enqueue(line);
            while (_recentProcessLines.Count > MaxBufferedCrashLines)
            {
                _recentProcessLines.Dequeue();
            }
        }
    }

    private void WriteCrashLog(Process process, int exitCode)
    {
        var builder = new StringBuilder();
        builder.AppendLine(new string('=', 80));
        builder.AppendLine($"Timestamp: {DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}");
        builder.AppendLine($"ExitCode: {exitCode}");
        builder.AppendLine($"Executable: {process.StartInfo.FileName}");
        builder.AppendLine($"WorkingDirectory: {process.StartInfo.WorkingDirectory}");
        builder.AppendLine($"Arguments: {string.Join(' ', process.StartInfo.ArgumentList)}");
        builder.AppendLine("Recent backend output:");

        lock (_crashLogLock)
        {
            if (_recentProcessLines.Count == 0)
            {
                builder.AppendLine("(no buffered backend output)");
            }
            else
            {
                foreach (var line in _recentProcessLines)
                {
                    builder.AppendLine(line);
                }
            }
        }

        builder.AppendLine();
        WriteCrashLogMessage(builder.ToString());
    }

    private static void WriteCrashLogMessage(string message)
    {
        try
        {
            File.AppendAllText(CrashLogPath, message + Environment.NewLine);
        }
        catch
        {
            // Keep crash logging best-effort.
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        Stop();
    }
}
