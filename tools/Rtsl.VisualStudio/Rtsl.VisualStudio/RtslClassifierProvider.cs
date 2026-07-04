using System.ComponentModel.Composition;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Text.Classification;
using Microsoft.VisualStudio.Utilities;

namespace Rtsl.VisualStudio
{
    [Export(typeof(IClassifierProvider))]
    [ContentType("rtsl")]
    internal sealed class RtslClassifierProvider : IClassifierProvider
    {
        [Import]
        internal IClassificationTypeRegistryService ClassificationRegistry = null;

        public IClassifier GetClassifier(ITextBuffer buffer)
        {
            return new RtslClassifier(ClassificationRegistry);
        }
    }
}