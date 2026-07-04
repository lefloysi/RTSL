using System.ComponentModel.Composition;
using System.Windows.Media;
using Microsoft.VisualStudio.Text.Classification;
using Microsoft.VisualStudio.Utilities;

namespace Rtsl.VisualStudio
{
    [Export(typeof(EditorFormatDefinition))]
    [ClassificationType(ClassificationTypeNames = RtslClassificationTypes.Keyword)]
    [Name(RtslClassificationTypes.Keyword)]
    [UserVisible(true)]
    internal sealed class RtslKeywordFormat : ClassificationFormatDefinition
    {
        public RtslKeywordFormat()
        {
            DisplayName = "RTSL Keyword";
            ForegroundColor = Colors.DeepSkyBlue;
            IsBold = true;
        }
    }
}