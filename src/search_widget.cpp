#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/search_result_model.hpp"

#include <QGroupBox>

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTableView>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>

namespace aiws::gui {

SearchWidget::SearchWidget(QWidget* parent) : QWidget(parent) {
    // TODO: Construct and lay out the Query, Search Results, and Selected Result
    // areas; configure the table/model; connect local events; establish initial state.

    auto* bx = new QGroupBox("Query", this);

    // query row
    query_edit_ = new QLineEdit(bx);
    query_edit_->setPlaceholderText("Enter a query");

    // number the queries
    result_count_spin_ = new QSpinBox(bx);
    result_count_spin_->setRange(1, 100);
    result_count_spin_->setValue(3);

    search_button_ = new QPushButton("Search", bx);

    // query field
    auto* query_row = new QHBoxLayout;
    query_row->addWidget(new QLabel("Query:", bx));
    query_row->addWidget(query_edit_, 1);
    query_row->addWidget(new QLabel("Results (k)", bx));
    query_row->addWidget(result_count_spin_);
    query_row->addWidget(search_button_);

    // results table
    result_model_ = new SearchResultModel(this);
    results_view_ = new QTableView(bx);
    results_view_->setModel(result_model_);
    //clicking selects a row
    results_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    results_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    // read only
    results_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    results_view_->setWordWrap(false);
    // hide row numbers
    results_view_->verticalHeader()->setVisible(false);
    //columns
    results_view_->horizontalHeader()->setStretchLastSection(true);
    results_view_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    results_view_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    results_view_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    results_view_->setColumnWidth(1, 160);

    auto* res_layout = new QVBoxLayout;
    res_layout->addWidget(new QLabel("Search Results", bx));
    res_layout->addWidget(results_view_);

    // selected result
    selected_result_text_ = new QPlainTextEdit(bx);
    selected_result_text_->setReadOnly(true);
    selected_result_text_->setPlaceholderText("Select a result to see its details.");
    view_source_button_ = new QPushButton("View Source", bx);

    auto* selected_layout = new QVBoxLayout;
    selected_layout->addWidget(new QLabel("Selected Result", bx));
    selected_layout->addWidget(selected_result_text_);
    selected_layout->addWidget(view_source_button_, 0, Qt::AlignRight);

    // Table on the Left
    auto* lower_row = new QHBoxLayout;
    lower_row->addLayout(res_layout, 3);
    lower_row->addLayout(selected_layout, 2);

    // Query row on the top
    auto* bx_layout = new QVBoxLayout(bx);
    bx_layout->addLayout(query_row);
    bx_layout->addLayout(lower_row, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(bx);

    //connect
    connect(search_button_, &QPushButton::clicked, this, &SearchWidget::request_search);
    connect(query_edit_, &QLineEdit::textChanged, this, &SearchWidget::query_changed);
    connect(results_view_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &SearchWidget::show_selected_result);
    connect(view_source_button_, &QPushButton::clicked, this, &SearchWidget::request_source_document);

    // start state
    search_button_->setEnabled(false);
    view_source_button_->setEnabled(false);

}

QString SearchWidget::query() const {
    return query_edit_->text();
}

int SearchWidget::result_count() const {
    return result_count_spin_->value();
}

bool SearchWidget::has_query() const {
    return !query_edit_->text().trimmed().isEmpty();
}

void SearchWidget::set_search_enabled(bool enabled) {
    search_button_->setEnabled(enabled);
}

void SearchWidget::set_results(std::vector<SearchResult> results) {
    clear_results();
    result_model_->set_results(std::move(results));
}

void SearchWidget::clear_results() {
    results_view_->clearSelection();
    result_model_->clear();
    selected_result_text_->clear();
    view_source_button_->setEnabled(false);
    emit selected_chunk_changed(QString());
}

void SearchWidget::reset() {
    query_edit_->clear();
    result_count_spin_->setValue(3);
    clear_results();
    set_search_enabled(false);
}

void SearchWidget::request_search() {
    emit search_requested(query(), result_count());
}

void SearchWidget::show_selected_result() {
    const QModelIndexList rows = results_view_->selectionModel()->selectedRows();
    const int row = rows.isEmpty() ? -1 : rows.first().row();
    const SearchResult* res = result_model_->result_at(row);

    // nothing valid is selected
    if (res == nullptr) {
        selected_result_text_->clear();
        view_source_button_->setEnabled(false);
        emit selected_chunk_changed(QString());
        return;
    }

    // build from data already in the model
    const QString details = QString("Rank: %1\nDocument: %2\nChunk: %3\nChunk ID: %4\n"
                            "Score: %5\nMatched Terms: %6\n\n%7")
                            .arg(row + 1)
                            .arg(QString::fromStdString(res->document_id))
                            .arg(static_cast<qulonglong>(res->chunk_sequence))
                            .arg(QString::fromStdString(res->chunk_id))
                            .arg(QString::number(res->score, 'f', 4))
                            .arg(static_cast<qulonglong>(res->matched_terms))
                            .arg(QString::fromStdString(res->text));

    selected_result_text_->setPlainText(details);
    view_source_button_->setEnabled(true);
    // tell term inspector which chunk TF should use
    emit selected_chunk_changed(QString::fromStdString(res->chunk_id));
}

void SearchWidget::request_source_document() {
    // TODO
    const QModelIndexList rows = results_view_->selectionModel()->selectedRows();
    const int row = rows.isEmpty() ? -1 : rows.first().row();
    const SearchResult* res = result_model_->result_at(row);
    if (res != nullptr) {
        emit source_document_requested(QString::fromStdString(res->document_id));
    }
}

}  // namespace aiws::gui
