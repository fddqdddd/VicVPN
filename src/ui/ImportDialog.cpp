#include "vicvpn/ui/ImportDialog.h"
#include "vicvpn/parser/ImportService.h"
#include "vicvpn/parser/SsconfCountry.h"
#include "vicvpn/app/I18n.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QTextEdit>
#include <QVBoxLayout>

namespace vicvpn {

ImportDialog::ImportDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(VTR("import.title"));
    resize(480, 400);

    auto* layout = new QVBoxLayout(this);
    tabs_ = new QTabWidget;

    auto* clipPage = new QWidget;
    auto* clipLay = new QVBoxLayout(clipPage);
    auto* clipBtn = new QPushButton(VTR("import.clipboard"));
    clipLay->addWidget(new QLabel(VTR("import.clipboard")));
    clipLay->addWidget(clipBtn);
    clipLay->addStretch();
    connect(clipBtn, &QPushButton::clicked, this, &ImportDialog::onClipboard);
    tabs_->addTab(clipPage, VTR("import.clipboard"));

    auto* urlPage = new QWidget;
    auto* urlLay = new QVBoxLayout(urlPage);
    urlEdit_ = new QLineEdit;
    urlEdit_->setPlaceholderText("https://... or ssconf://...");
    urlLay->addWidget(new QLabel(VTR("import.url")));
    urlLay->addWidget(urlEdit_);
    connect(urlEdit_, &QLineEdit::textChanged, this, &ImportDialog::onInputChanged);
    tabs_->addTab(urlPage, VTR("import.url"));

    auto* manualPage = new QWidget;
    auto* manLay = new QVBoxLayout(manualPage);
    manualEdit_ = new QTextEdit;
    manualEdit_->setPlaceholderText("vless://...\nvmess://...\n{ JSON }");
    manLay->addWidget(new QLabel(VTR("import.manual")));
    manLay->addWidget(manualEdit_);
    connect(manualEdit_, &QTextEdit::textChanged, this, &ImportDialog::onInputChanged);
    tabs_->addTab(manualPage, VTR("import.manual"));

    countryLabel_ = new QLabel(VTR("import.country"));
    countryCombo_ = new QComboBox;
    for (const auto& c : SsconfCountry::all()) {
        const QString label = I18n::instance().langCode() == "en" ? c.nameEn : c.nameRu;
        countryCombo_->addItem(label, c.code);
    }
    countryLabel_->setVisible(false);
    countryCombo_->setVisible(false);
    layout->addWidget(tabs_);
    layout->addWidget(countryLabel_);
    layout->addWidget(countryCombo_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(VTR("import.ok"));
    buttons->button(QDialogButtonBox::Cancel)->setText(VTR("import.cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &ImportDialog::onOk);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(tabs_, &QTabWidget::currentChanged, this, [this]() { onInputChanged(); });
}

ImportOptions ImportDialog::importOptions() const {
    ImportOptions opts;
    if (countryCombo_ && countryCombo_->isVisible())
        opts.ssconfCountry = countryCombo_->currentData().toString();
    return opts;
}

void ImportDialog::updateCountryUi(const QString& text) {
    const bool ssconf = SsconfCountry::isSsconfUri(text);
    countryLabel_->setVisible(ssconf);
    countryCombo_->setVisible(ssconf);
}

void ImportDialog::onInputChanged() {
    QString text;
    if (tabs_->currentIndex() == 1)
        text = urlEdit_->text();
    else if (tabs_->currentIndex() == 2)
        text = manualEdit_->toPlainText();
    else
        text = QApplication::clipboard()->text();
    updateCountryUi(text.trimmed());
}

void ImportDialog::onClipboard() {
    const QString text = QApplication::clipboard()->text().trimmed();
    if (text.isEmpty()) return;
    QString err;
    imported_ = ImportService::importText(text, &err, importOptions());
    if (imported_.empty() && !err.isEmpty())
        QMessageBox::warning(this, VTR("import.title"), err);
    if (!imported_.empty())
        accept();
}

void ImportDialog::onUrl() {
    onOk();
}

void ImportDialog::onManual() {}

void ImportDialog::onOk() {
    QString text;
    if (tabs_->currentIndex() == 1)
        text = urlEdit_->text().trimmed();
    else if (tabs_->currentIndex() == 2)
        text = manualEdit_->toPlainText().trimmed();
    else
        text = QApplication::clipboard()->text().trimmed();
    if (text.isEmpty()) return;
    QString err;
    imported_ = ImportService::importText(text, &err, importOptions());
    if (imported_.empty()) {
        if (text.startsWith('{') || text.startsWith('['))
            imported_ = ImportService::importJson(text, &err);
    }
    if (imported_.empty()) {
        QMessageBox::warning(this, VTR("import.title"), err.isEmpty() ? "Parse failed" : err);
        return;
    }
    accept();
}

} // namespace vicvpn
