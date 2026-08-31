using System.ComponentModel.Composition;
using Microsoft.VisualStudio.Utilities;

namespace RTSL.VisualStudio
{

internal static class RtslContentType
{
    public const string Name = "RTSL";

    [Export, Name(Name), BaseDefinition("code")]
    internal static ContentTypeDefinition Definition = null!;

    [Export, FileExtension(".rtsl"), ContentType(Name)]
    internal static FileExtensionToContentTypeDefinition SourceExtension = null!;

    [Export, FileExtension(".rtslm"), ContentType(Name)]
    internal static FileExtensionToContentTypeDefinition ModuleExtension = null!;
}
}
