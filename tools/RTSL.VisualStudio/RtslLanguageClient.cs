using System;
using System.ComponentModel.Composition;
using System.Diagnostics;
using System.IO;
using System.Collections.Generic;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.VisualStudio.LanguageServer.Client;
using Microsoft.VisualStudio.Threading;
using Microsoft.VisualStudio.Utilities;

namespace RTSL.VisualStudio
{

// This is deliberately thin: the native compiler frontend owns every language answer.
[Export(typeof(ILanguageClient)), ContentType(RtslContentType.Name), Name("RTSL Language Client")]
internal sealed class RtslLanguageClient : ILanguageClient
{
    public string Name => "RTSL";
    public IEnumerable<string> ConfigurationSections => Array.Empty<string>();
    public object InitializationOptions => null!;
    public IEnumerable<string> FilesToWatch => Array.Empty<string>();
    public bool ShowNotificationOnInitializeFailed => true;
    public event AsyncEventHandler<EventArgs> StartAsync;
    public event AsyncEventHandler<EventArgs> StopAsync;

    public Task<Connection> ActivateAsync(CancellationToken token)
    {
        var extensionDirectory = Path.GetDirectoryName(typeof(RtslLanguageClient).Assembly.Location)!;
        var server = Path.Combine(extensionDirectory, "rtsl-lsp.exe");
        if (!File.Exists(server)) throw new FileNotFoundException("The RTSL language server was not packaged with this VSIX.", server);
        var process = Process.Start(new ProcessStartInfo(server) { UseShellExecute = false, RedirectStandardInput = true, RedirectStandardOutput = true, CreateNoWindow = true })!;
        return Task.FromResult(new Connection(process.StandardOutput.BaseStream, process.StandardInput.BaseStream));
    }

    public Task OnLoadedAsync() => StartAsync != null
        ? StartAsync.InvokeAsync(this, EventArgs.Empty)
        : Task.CompletedTask;
    public Task OnServerInitializedAsync() => Task.CompletedTask;
    public Task<InitializationFailureContext> OnServerInitializeFailedAsync(ILanguageClientInitializationInfo initializationInfo)
        => Task.FromResult(default(InitializationFailureContext));
}
}
