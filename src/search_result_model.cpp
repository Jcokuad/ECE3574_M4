#include "aiws/gui/search_result_model.hpp"

#include <QString>
#include <utility>

namespace aiws::gui {

SearchResultModel::SearchResultModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int SearchResultModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(results_.size());
}

int SearchResultModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return 5; // should only be 5 columns for rank, document, chunk, score, and text
}

QVariant SearchResultModel::data(const QModelIndex& index, int role) const {
    // protects against bad indexes
    const SearchResult* res = index.isValid() ? result_at(index.row()) : nullptr;
    if (res == nullptr) {
        return {};
    }
    
    // text shown in the cell
    if (role == Qt::DisplayRole) {
        switch (index.column()) {

        case 0: // Rank
            return index.row() + 1; 
        case 1: // Document: document ID
            return QString::fromStdString(res->document_id);
        case 2: // Chunk
            // qulonglong is Qt's unsigned int type
            return static_cast<qulonglong>(res->chunk_sequence);
        case 3: // Score
            return QString::number(res->score, 'f', 4);
        case 4: // Text: text in the chunk
            return QString::fromStdString(res->text).simplified();
        default:
            return {};
        }
    }

    if (role == Qt::TextAlignmentRole) {
        // Text columns doc = 1 or text = 4 align to the left
        if (index.column() == 1 || index.column() == 4) {
            return QVariant(Qt::AlignLeft | Qt::AlignVCenter);
        }
        // number columns align to the right
        return QVariant(Qt::AlignRight | Qt::AlignVCenter);
    }

    // use defaults for everything else
    return {};
}

QVariant SearchResultModel::headerData(int section,
                                       Qt::Orientation orientation,
                                       int role) const {
    
    // column titles
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }
    switch (section) {

    case 0:
        return QString("Rank");
    case 1:
        return QString("Document");
    case 2:
        return QString("Chunk");
    case 3:
        return QString("Score");
    case 4:
        return QString("Text");
    default:
        return {};
    }
}

void SearchResultModel::set_results(std::vector<SearchResult> results) {
    // replaces results in the table with a new set after a search
    beginResetModel();
    results_ = std::move(results); // don't copy the vectors contents
    endResetModel();
}

void SearchResultModel::clear() {
    // empties the table when results are reset
    beginResetModel();
    results_.clear();
    endResetModel();
}

const SearchResult* SearchResultModel::result_at(int row) const noexcept {
    // gives other code read only access to teh full SearchResult
    if (row < 0 || static_cast<std::size_t>(row) >= results_.size()) {
        return nullptr;
    }

    // Return a pointer into the vector
    return &results_[static_cast<std::size_t>(row)];
}

}  // namespace aiws::gui
