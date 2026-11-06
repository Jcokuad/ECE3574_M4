#include "aiws/gui/workspace_widget.hpp"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace aiws::gui {

WorkspaceWidget::WorkspaceWidget(QWidget* parent) : QWidget(parent) {
    // TODO: Construct and lay out the Workspace and Selected Document areas,
    // connect local widget events, and establish initial presentation state.
    
    // left box
    auto* workspace_bx = new QGroupBox("Workspace", this);

    document_list_ = new QListWidget(workspace_bx);
    document_list_->setSelectionMode(QAbstractItemView::SingleSelection);

    add_document_button_ = new QPushButton("Add Document", workspace_bx);
    build_corpus_button_ = new QPushButton("Build Corpus", workspace_bx);
    corpus_status_label_ = new QLabel("Corpus: Not Built", workspace_bx);

    // put the two buttons next to each other
    auto* button_row = new QHBoxLayout;
    button_row->addWidget(add_document_button_);
    button_row->addWidget(build_corpus_button_);
    button_row->addStretch();

    // stack list, buttons, and status inside the box
    auto* workspace_layout = new QVBoxLayout(workspace_bx);
    workspace_layout->addWidget(document_list_);
    workspace_layout->addLayout(button_row);
    workspace_layout->addWidget(corpus_status_label_);

    // right box: selected document
    auto* selected_doc_bx = new QGroupBox("Selected Document", this);

    // Title label
    document_title_label_ = new QLabel(selected_doc_bx);
    QFont title_font = document_title_label_->font();
    title_font.setBold(true);
    document_title_label_->setFont(title_font);

    // set as read only
    document_text_ = new QPlainTextEdit(selected_doc_bx);
    document_text_->setReadOnly(true);
    document_text_->setPlaceholderText("Select a document to view its details!");

    auto* doc_layout = new QVBoxLayout(selected_doc_bx);
    doc_layout->addWidget(document_title_label_);
    doc_layout->addWidget(document_text_);

    // place the two boxes next to each other
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(workspace_bx, 2);
    layout->addWidget(selected_doc_bx, 3);

    // connect each qt signal to the widgets own signals
    connect(add_document_button_, &QPushButton::clicked, this, &WorkspaceWidget::add_document_requested);
    connect(build_corpus_button_, &QPushButton::clicked, this, &WorkspaceWidget::build_corpus_requested);
    connect(document_list_, &QListWidget::currentRowChanged, this, &WorkspaceWidget::document_selected);

    // start with nothing
    build_corpus_button_->setEnabled(false);
}

void WorkspaceWidget::add_document(const QString& title) {
    document_list_->addItem(title);
}

void WorkspaceWidget::select_document(int row) {
    document_list_->setCurrentRow(row);
}

void WorkspaceWidget::show_selected_document(const QString& title,
                                              const QString& text) {
    document_title_label_->setText(title);
    document_text_->setPlainText(text);
}

void WorkspaceWidget::clear_selected_document() {
    document_title_label_->clear();
    document_text_->clear();
}

void WorkspaceWidget::set_corpus_status(const QString& text) {
    corpus_status_label_->setText(text);
}

void WorkspaceWidget::set_build_corpus_enabled(bool enabled) {
    build_corpus_button_->setEnabled(enabled);
}

void WorkspaceWidget::reset() {
    document_list_->clear();
    clear_selected_document();
    set_corpus_status("Corpus: Not Built");
    set_build_corpus_enabled(false);
}

}  // namespace aiws::gui
