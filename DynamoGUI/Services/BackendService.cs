using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace DynamoGUI.Services;

public sealed class BackendService : IDisposable
{
    public const string BackendExecutableName = "DynamoAPI.exe";

    private Process? _process;
    private bool _disposed;

    public event Action<string>? OnOutput;
    public event Action<string>? OnError;
    public event Action<int>? OnExited;

    public bool IsRunning => _process is { HasExited: false };

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
            var startInfo = new ProcessStartInfo
            {
                FileName = executablePath,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
                WorkingDirectory = ResolveWorkingDirectory(executablePath),
            };

            foreach (var argument in arguments ?? Enumerable.Empty<string>())
            {
                startInfo.ArgumentList.Add(argument);
            }

            _process = new Process { StartInfo = startInfo, EnableRaisingEvents = true };
            _process.OutputDataReceived += (_, e) => { if (!string.IsNullOrWhiteSpace(e.Data)) OnOutput?.Invoke(e.Data); };
            _process.ErrorDataReceived += (_, e) => { if (!string.IsNullOrWhiteSpace(e.Data)) OnError?.Invoke(e.Data); };
            _process.Exited += (_, _) => OnExited?.Invoke(_process?.ExitCode ?? -1);

            _process.Start();
            _process.BeginOutputReadLine();
            _process.BeginErrorReadLine();

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
            OnError?.Invoke($"Failed to start backend: {ex.Message}");
            return false;
        }
    }

    public void Stop()
    {
        if (_process is null || _process.HasExited)
        {
            return;
        }

        try
        {
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

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        Stop();
    }
}
