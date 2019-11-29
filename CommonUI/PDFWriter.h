#ifndef __PDF_WRITER_H__
#define __PDF_WRITER_H__

#include <QPrinter>
#include <QTextDocument>
#include <QString>
#include <QUrl>
#include <QVariant>


class PDFWriter
{
public:
   PDFWriter(const QString &templateFN, const QUrl &baseUrl = {});

   //bool substitute(const QVariantHash &vars);
   bool output(const QString &outputFN);

private:
   QPrinter       printer_;
   QTextDocument  doc_;
   QString        templateText_;
   QString        substitutedText_;
};

#endif // __PDF_WRITER_H__
