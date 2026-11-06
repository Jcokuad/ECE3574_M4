#include "aiws/gui/term_inspector_widget.hpp"

#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>

namespace aiws::gui {

TermInspectorWidget::TermInspectorWidget(QWidget* parent) : QWidget(parent) {
    // TODO: Construct and lay out the Term Inspector, connect local events,
    // and establish initial state.

    // user enters a term and clicks inspect
    auto* bx = new QGroupBox("Term Inspector", this);

    // term row
    term_edit_ = new QLineEdit(bx);
    term_edit_->setPlaceholderText("Enter a term");
    inspect_button_ = new QPushButton("Inspect", bx);

    auto* term_row = new QHBoxLayout;
    term_row->addWidget(new QLabel("Term:", bx));
    term_row->addWidget(term_edit_, 1);
    term_row->addWidget(inspect_button_);

    // stats
    document_frequency_value_ = new QLabel("-", bx);
    chunk_frequency_value_ = new QLabel("-", bx);

    auto* stats = new QFormLayout;
    stats->addRow("Document frequency:", document_frequency_value_);
    stats->addRow("Selected-chunk TF", chunk_frequency_value_);

    auto* bx_layout = new QVBoxLayout(bx);
    bx_layout->addLayout(term_row);
    bx_layout->addLayout(stats);
    bx_layout->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(bx);

    // clicking inspect will request an inspection
    connect(inspect_button_, &QPushButton::clicked, this, &TermInspectorWidget::request_inspection);

    // start state
    inspect_button_->setEnabled(false);
}

void TermInspectorWidget::set_inspection_enabled(bool enabled) {
    inspect_button_->setEnabled(enabled);
}

void TermInspectorWidget::set_selected_chunk(const QString& chunk_id) {
    selected_chunk_id_ = chunk_id;
    chunk_frequency_value_->setText("-");
}

void TermInspectorWidget::set_statistics(std::size_t document_frequency,
                                         bool has_chunk_frequency,
                                         std::size_t chunk_frequency) {
    document_frequency_value_->setText(QString::number(static_cast<qulonglong>(document_frequency)));
    chunk_frequency_value_->setText(has_chunk_frequency ? QString::number(static_cast<qulonglong>(chunk_frequency)) : QString("-"));
}

void TermInspectorWidget::clear_statistics() {
    document_frequency_value_->setText("-");
    chunk_frequency_value_->setText("-");
}

void TermInspectorWidget::reset() {
    term_edit_->clear();
    selected_chunk_id_.clear();
    clear_statistics();
    set_inspection_enabled(false);
}

void TermInspectorWidget::request_inspection() {
    const QString term = term_edit_->text().trimmed();
    if (term.isEmpty()) {
        return; // empty makes no request
    }
    emit term_inspection_requested(term, selected_chunk_id_);
}

}  // namespace aiws::gui
