// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "base/unique_qptr.h"
#include "ui/rp_widget.h"

#include <optional>
#include <utility>

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace MonitorCenter {

// Chronological feed of downloaded files, grouped by message, with
// summary tiles on top and target/type/status filter chips.
class ActivityView final : public Ui::RpWidget {
public:
	ActivityView(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Fn<void()> scrollToTop);

	// Scroll container hooks: append the next page when the viewport
	// approaches the end of the loaded content.
	void checkLoadMore(int scrollTop, int viewportHeight);

	// Refresh the summary tiles (called when the view is shown).
	void refreshStats();

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	int resizeGetHeight(int newWidth) override;

private:
	struct VersionRow {
		long long fakeId = 0;
		QString name;
		QString status;
		QString meta;
		int statusColor = 0; // palette role index, see kStatus* constants
		bool done = false;
		QString path;
	};

	struct Group {
		long long peerId = 0;
		int messageId = 0;
		QString header;
		std::vector<VersionRow> rows;
		int top = 0;
		int height = 0;
	};

	void loadPage();
	void resetHistory();
	void refreshFinishedRows(const std::vector<QString> &finished);
	std::optional<std::pair<int, int>> hitVersionRow(QPoint pos) const;
	void showFileMenu(QPoint globalPos, const VersionRow &row);
	void showFilterMenu(int chipIndex, QPoint globalPos);
	void clearHistory();

	const not_null<Window::SessionController*> _controller;

	// Tiles.
	QString _todayTile;
	QString _totalTile;
	QString _failedTile;
	int _failedCount = 0;

	// Filters (0 = all targets / all types / all statuses).
	std::vector<std::pair<long long, QString>> _targetOptions;
	int _targetFilter = 0;
	int _typeFilter = 0;
	int _statusFilter = 0;
	std::vector<QString> _typeOptions;
	std::vector<QString> _typeNames;
	std::vector<QString> _statusOptions;

	std::vector<Group> _groups;
	std::set<std::pair<long long, int>> _groupedMessages;
	long long _oldestFakeId = 0;
	std::set<QString> _lastActivePaths;
	bool _endReached = false;
	bool _loading = false;
	int _contentHeight = 0;

	base::unique_qptr<Ui::PopupMenu> _menu;
	Fn<void()> _scrollToTop;

};

} // namespace MonitorCenter
