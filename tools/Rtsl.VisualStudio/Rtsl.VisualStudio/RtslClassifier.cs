using System;
using System.Collections.Generic;
using Microsoft.VisualStudio.Text;
using Microsoft.VisualStudio.Text.Classification;

namespace Rtsl.VisualStudio
{
    internal sealed class RtslClassifier : IClassifier
    {
        private readonly IClassificationType _keywordType;

        public event EventHandler<ClassificationChangedEventArgs> ClassificationChanged;

        public RtslClassifier(IClassificationTypeRegistryService registry)
        {
            _keywordType = registry.GetClassificationType(RtslClassificationTypes.Keyword);
        }

        public IList<ClassificationSpan> GetClassificationSpans(SnapshotSpan span)
        {
            var result = new List<ClassificationSpan>();
            string text = span.GetText();

            int index = text.IndexOf("struct", StringComparison.Ordinal);

            if (index >= 0)
            {
                var wordSpan = new SnapshotSpan(
                    span.Snapshot,
                    span.Start + index,
                    "struct".Length);

                result.Add(new ClassificationSpan(wordSpan, _keywordType));
            }

            return result;
        }
    }
}