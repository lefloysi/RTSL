using Microsoft.VisualStudio.Text.Classification;
using Microsoft.VisualStudio.Utilities;
using System.ComponentModel.Composition;

namespace Rtsl.VisualStudio
{
    internal static class RtslClassificationTypes
    {
        public const string Keyword = "rtsl.keyword";

        [Export(typeof(ClassificationTypeDefinition))]
        [Name(Keyword)]
        internal static ClassificationTypeDefinition KeywordDefinition;
    }
}