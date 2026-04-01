# DynamoGUI

Avalonia 11 desktop shell for the active Dynamo backend host.

This project is the primary GUI path for the repository and follows the same split used in `tools/RUIT` + `tools/RuitGUI`:

- native C++ backend process
- named-pipe IPC
- Avalonia C# desktop shell

## Current scope

- launch or connect to an external backend process
- named-pipe client for a small Dynamo IPC protocol
- overview, profiles, backend and logs shell sections
- production desktop frontend for `dynamo_backend_host`

## Build

```powershell
dotnet build DynamoGUI\DynamoGUI.csproj
dotnet build DynamoGUI\DynamoGUI.csproj -c Release
```

## Run

```powershell
dotnet run --project DynamoGUI\DynamoGUI.csproj
```

Or launch the built executable:

```powershell
DynamoGUI\bin\Release\net8.0\Dynamo.exe
```

## Production Bundle

Create a single runnable folder containing:

- `DynamoGUI.exe`
- `dynamo_backend_host.exe`
- native DLL dependencies
- `config/profiles`

using:

```powershell
powershell -ExecutionPolicy Bypass -File .\publish-dynamo.ps1
```

If the GUI was already built and you only want to stage a clean deploy folder from existing binaries:

```powershell
powershell -ExecutionPolicy Bypass -File .\publish-dynamo.ps1 -SkipGuiBuild
```

The staged output is written to `dist\Dynamo`.

## Backend expectations

The production layout expects `dynamo_backend_host.exe` in the same folder as `DynamoGUI.exe`.
Development fallback still checks the local backend build output.

Default named pipe: `DYNAMO_IPC`

## Next step

Create a dedicated C++ backend host that exposes:

- status snapshots
- profile list snapshots
- load/save/start/stop/shutdown commands
- structured log events
