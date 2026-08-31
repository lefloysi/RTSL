using System.ComponentModel.Composition;
using System.Windows.Media;
using Microsoft.VisualStudio.Text.Classification;
using Microsoft.VisualStudio.Utilities;

namespace RTSL.VisualStudio
{
internal static class RtslLexicalColors
{
    internal const string Attribute = "rtsl.attribute";
    internal const string Comment = "rtsl.comment";
    internal const string ControlKeyword = "rtsl.controlKeyword";
}

[Export, Name(RtslLexicalColors.Attribute), BaseDefinition("text")]
internal static class RtslAttributeClassification { }
[Export, Name(RtslLexicalColors.Comment), BaseDefinition("text")]
internal static class RtslCommentClassification { }
[Export, Name(RtslLexicalColors.ControlKeyword), BaseDefinition("text")]
internal static class RtslControlKeywordClassification { }

[Export(typeof(EditorFormatDefinition)), ClassificationType(ClassificationTypeNames = RtslLexicalColors.Attribute), Name("RTSL Attribute"), DisplayName("RTSL Attribute"), UserVisible(true), Order(After = Priority.Default, Before = Priority.High)]
internal sealed class RtslAttributeFormat : ClassificationFormatDefinition
{
    public RtslAttributeFormat() { ForegroundColor = Color.FromRgb(176, 176, 176); }
}

[Export(typeof(EditorFormatDefinition)), ClassificationType(ClassificationTypeNames = RtslLexicalColors.Comment), Name("RTSL Comment"), DisplayName("RTSL Comment"), UserVisible(true), Order(After = Priority.Default, Before = Priority.High)]
internal sealed class RtslCommentFormat : ClassificationFormatDefinition
{
    public RtslCommentFormat() { ForegroundColor = Color.FromRgb(106, 153, 85); }
}

[Export(typeof(EditorFormatDefinition)), ClassificationType(ClassificationTypeNames = RtslLexicalColors.ControlKeyword), Name("RTSL Control Keyword"), DisplayName("RTSL Control Keyword"), UserVisible(true), Order(After = Priority.Default, Before = Priority.High)]
internal sealed class RtslControlKeywordFormat : ClassificationFormatDefinition
{
    public RtslControlKeywordFormat() { ForegroundColor = Color.FromRgb(197, 134, 192); }
}
}
