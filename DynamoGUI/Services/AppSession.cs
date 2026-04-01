using System;

namespace DynamoGUI.Services;

public sealed class AppSession : IDisposable
{
    private bool _disposed;

    public BackendService Backend { get; }
    public IpcClient Ipc { get; }

    public string PipeName => Ipc.PipeName;

    public AppSession()
        : this(new BackendService(), new IpcClient())
    {
    }

    public AppSession(BackendService backend, IpcClient ipc)
    {
        Backend = backend ?? throw new ArgumentNullException(nameof(backend));
        Ipc = ipc ?? throw new ArgumentNullException(nameof(ipc));
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        Ipc.Dispose();
        Backend.Dispose();
    }
}
