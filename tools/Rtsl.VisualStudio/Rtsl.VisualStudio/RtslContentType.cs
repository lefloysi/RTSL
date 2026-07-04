using Microsoft.VisualStudio.Utilities;
using System.ComponentModel.Composition;

namespace Rtsl.VisualStudio
{
    internal static class RtslContentType
    {
        [Export]
        [Name("rtsl")]
        [BaseDefinition("code")]
        internal static ContentTypeDefinition ContentTypeDefinition;

        [Export]
        [FileExtension(".rtsl")]
        [ContentType("rtsl")]
        internal static FileExtensionToContentTypeDefinition FileExtensionDefinition;
    }
}