#pragma once

class QTextDocument;
class QString;

namespace syntax {

// Installs a lightweight, theme-aware syntax highlighter on the document for the
// given language (e.g. "python", "js", "html", "css", "sql", "bash"). The
// highlighter is parented to the document, so it is recreated whenever the
// document is rebuilt and therefore always matches the current light/dark theme.
void install(QTextDocument *document, const QString &language);

} // namespace syntax