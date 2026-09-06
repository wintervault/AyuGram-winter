// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/monitor/monitor_center_activity.h"

#include "ayu/data/ayu_database.h"
#include "ayu/data/entities.h"
#include "ayu/features/monitor/monitor_queue.h"
#include "ayu/ui/monitor/monitor_center.h"
#include "base/unixtime.h"
#include "base/unique_qptr.h"
#include "core/file_utilities.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_boxes.h"
#include "styles/style_window.h"
#include "ui/painter.h"
#include "ui/style/style_core_scale.h"
#include "ui/widgets/popup_menu.h"
#include "window/window_session_controller.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>
#include <map>
#include <set>

namespace MonitorCenter {
namespace {

constexpr auto kPageSize = 50;

// Handwritten pixel constants below are "design pixels" for a 13px font:
// font metrics (normalFont->height = 26 at 2x) are already scaled, so
// every constant goes through style::ConvertScale to match them.
[[nodiscard]] int RightMargin() {
	// The overlay scrollbar (style width 14 + deltax 5) is scaled already.
	return style::ConvertScale(19);
}
[[nodiscard]] int TilesHeight() {
	return st::semiboldFont->height
		+ style::ConvertScale(4)
		+ st::normalFont->height
		+ 2 * style::ConvertScale(12);
}
[[nodiscard]] int FiltersHeight() {
	return st::normalFont->height
		+ style::ConvertScale(10)
		+ 2 * style::ConvertScale(6);
}
[[nodiscard]] int ChipHeight() {
	return st::normalFont->height + style::ConvertScale(10);
}
[[nodiscard]] int GroupHeaderHeight() {
	return st::semiboldFont->height + style::ConvertScale(10);
}
[[nodiscard]] int VersionHeight() {
	return st::normalFont->height + style::ConvertScale(4);
}
[[nodiscard]] int GroupPad() {
	return style::ConvertScale(8);
}
[[nodiscard]] int BottomPad() {
	return style::ConvertScale(14);
}

constexpr auto kStatusDone = 0;
constexpr auto kStatusPending = 1;
constexpr auto kStatusFailed = 2;

[[nodiscard]] QString TimeText(int unixtime) {
	const auto parsed = base::unixtime::parse(unixtime);
	return (parsed.date() == QDate::currentDate())
		? parsed.time().toString(u"HH:mm"_q)
		: parsed.date().toString(u"MM-dd HH:mm"_q);
}

[[nodiscard]] QString DateText(int unixtime) {
	return base::unixtime::parse(unixtime).date().toString(u"MM-dd HH:mm"_q);
}

[[nodiscard]] QString StatusText(const MonitorFile &file, int *color) {
	if (file.status == int(MonitorFileStatus::done)) {
		*color = kStatusDone;
		return TimeText(file.downloadedDate);
	} else if (file.status == int(MonitorFileStatus::failed)) {
		*color = kStatusFailed;
		return u"Failed"_q;
	}
	*color = kStatusPending;
	return u"Pending"_q;
}

} // namespace

ActivityView::ActivityView(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Ui::RpWidget(parent)
, _controller(controller) {
	_typeOptions = { u"All"_q };
	const auto names = {
		u"Photo"_q,
		u"Video"_q,
		u"Voice"_q,
		u"Audio"_q,
		u"Video note"_q,
		u"GIF"_q,
		u"Document"_q,
	};
	const auto types = {
		std::string("photo"),
		std::string("video"),
		std::string("voice"),
		std::string("audio"),
		std::string("video_note"),
		std::string("gif"),
		std::string("document"),
	};
	auto nameIt = names.begin();
	for (const auto &type : types) {
		_typeNames.push_back(QString::fromStdString(type));
		_typeOptions.push_back(*nameIt);
		++nameIt;
	}
	_statusOptions = {
		u"All"_q,
		u"Downloading"_q,
		u"Done"_q,
		u"Failed"_q,
		u"Replaced"_q,
	};
	// loadPage reads this; the full list arrives in refreshStats().
	_targetOptions.emplace_back(0, u"All targets"_q);

	loadPage();

	// Live overlay: repaint on queue changes, refresh rows that just
	// finished (their DB status moved on since the page was loaded).
	AyuFeatures::Monitor::QueueChanged(
	) | rpl::on_next([=] {
		const auto snap = AyuFeatures::Monitor::SnapshotQueue();
		const auto current = std::set<QString>(
			snap.activePaths.begin(),
			snap.activePaths.end());
		auto finished = std::vector<QString>();
		for (const auto &path : _lastActivePaths) {
			if (!current.contains(path)) {
				finished.push_back(path);
			}
		}
		_lastActivePaths = current;
		if (!finished.empty()) {
			refreshFinishedRows(finished);
			refreshStats();
		}
		update();
	}, lifetime());
}

void ActivityView::refreshStats() {
	const auto session = &_controller->session();
	const auto userId = session->userId().bare & PeerId::kChatTypeMask;
	const auto now = base::unixtime::now();
	const auto dayStart = int(QDateTime(
		base::unixtime::parse(now).date(),
		QTime(0, 0)).toSecsSinceEpoch());
	const auto stats = AyuDatabase::Monitor::getGlobalStats(userId, dayStart);

	_todayTile = u"+%1 · %2"_q
		.arg(stats.todayCount)
		.arg(MonitorFormatBytes(stats.todayBytes));
	_totalTile = u"%1 · %2"_q
		.arg(stats.totalCount)
		.arg(MonitorFormatBytes(stats.totalBytes));
	_failedTile = (stats.failedCount > 0)
		? QString::number(stats.failedCount)
		: u"–"_q;
	_failedCount = stats.failedCount;

	// Target filter options: all monitored targets plus any target that
	// has downloads. Keep the current selection if it survives rebuild.
	const auto selected = (_targetFilter >= 0
		&& _targetFilter < int(_targetOptions.size()))
		? _targetOptions[_targetFilter].first
		: 0;
	auto seen = std::map<long long, QString>();
	for (const auto &target : AyuDatabase::Monitor::getAllMonitorTargets(userId)) {
		seen.emplace(target.peerId, MonitorPeerName(_controller, target.peerId));
	}
	for (const auto &targetStats : AyuDatabase::Monitor::getTargetStats(userId)) {
		seen.emplace(targetStats.peerId, MonitorPeerName(_controller, targetStats.peerId));
	}
	_targetOptions.clear();
	_targetOptions.emplace_back(0, u"All targets"_q);
	_targetFilter = 0;
	for (const auto &[peerId, name] : seen) {
		_targetOptions.emplace_back(peerId, name);
		if (peerId == selected) {
			_targetFilter = int(_targetOptions.size()) - 1;
		}
	}
	update();
}

void ActivityView::loadPage() {
	if (_loading || _endReached) {
		return;
	}
	_loading = true;
	const auto session = &_controller->session();
	const auto userId = session->userId().bare & PeerId::kChatTypeMask;

	auto filter = MonitorFileFilter();
	filter.peerId = _targetOptions[_targetFilter].first;
	filter.type = (_typeFilter > 0)
		? _typeNames[_typeFilter - 1].toStdString()
		: std::string();
	switch (_statusFilter) {
	case 1: filter.status = int(MonitorFileStatus::pending); break;
	case 2: filter.status = int(MonitorFileStatus::done); break;
	case 3: filter.status = int(MonitorFileStatus::failed); break;
	case 4: filter.minVersion = 2; break;
	default: break;
	}

	const auto page = AyuDatabase::Monitor::getMonitorFilesPage(
		userId,
		filter,
		_oldestFakeId,
		kPageSize);
	_endReached = page.endReached;
	_oldestFakeId = page.nextFakeId;
	if (!page.rows.empty()) {
		// Merge rows into groups by (peerId, messageId), loading the
		// full version list of every message (index-backed query).
		auto current = std::optional<Group>();
		for (const auto &row : page.rows) {
			const auto key = std::pair(row.peerId, row.messageId);
			if (_groupedMessages.contains(key)) {
				// This message was already grouped on an earlier page.
				continue;
			}
			if (current.has_value()
				&& current->peerId == row.peerId
				&& current->messageId == row.messageId) {
				// Versions are already complete from the first row.
				continue;
			}
			if (current.has_value()) {
				_groups.push_back(std::move(*current));
			}
			auto group = Group();
			group.peerId = row.peerId;
			group.messageId = row.messageId;
			const auto versions = AyuDatabase::Monitor::getMonitorVersions(
				userId,
				row.peerId,
				row.messageId);
			for (const auto &version : versions) {
				auto line = VersionRow();
				line.fakeId = version.fakeId;
				line.name = QFileInfo(
					QString::fromStdString(version.filePath)).fileName();
				line.meta = u"v%1 · %2"_q
					.arg(version.version)
					.arg(MonitorFormatBytes(version.fileSize));
				line.done = (version.status == int(MonitorFileStatus::done));
				line.status = StatusText(version, &line.statusColor);
				line.path = QString::fromStdString(version.filePath);
				group.rows.push_back(std::move(line));
			}
			group.header = MonitorPeerName(_controller, row.peerId)
				+ u"  ·  #%1"_q.arg(row.messageId);
			current = std::move(group);
		}
		if (current.has_value()) {
			_groups.push_back(std::move(*current));
		}
	}
	resizeToWidth(width());
	update();
	_loading = false;
}

void ActivityView::refreshFinishedRows(
		const std::vector<QString> &finished) {
	const auto session = &_controller->session();
	const auto userId = session->userId().bare & PeerId::kChatTypeMask;
	for (auto &group : _groups) {
		for (auto &row : group.rows) {
			if (std::find(finished.begin(), finished.end(), row.path)
				== finished.end()) {
				continue;
			}
			const auto fresh = AyuDatabase::Monitor::getMonitorFileById(
				userId,
				row.fakeId);
			if (!fresh.has_value()) {
				continue;
			}
			row.done = (fresh->status == int(MonitorFileStatus::done));
			row.status = StatusText(*fresh, &row.statusColor);
			row.meta = u"v%1 · %2"_q
				.arg(fresh->version)
				.arg(MonitorFormatBytes(fresh->fileSize));
		}
	}
}

void ActivityView::checkLoadMore(int scrollTop, int viewportHeight) {
	if (!_loading
		&& !_endReached
		&& scrollTop + viewportHeight > _contentHeight - 400) {
		loadPage();
	}
}

int ActivityView::resizeGetHeight(int newWidth) {
	auto y = TilesHeight() + FiltersHeight();
	for (auto &group : _groups) {
		group.top = y;
		group.height = GroupHeaderHeight()
			+ int(group.rows.size()) * VersionHeight()
			+ GroupPad();
		y += group.height;
	}
	_contentHeight = y + BottomPad()
		+ (_endReached ? 0 : GroupHeaderHeight());
	return _contentHeight;
}

void ActivityView::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto w = width();

	// Tiles row.
	const auto tileWidth = w / 4;
	const auto captions = {
		u"Active / Queue"_q,
		u"Today"_q,
		u"Total"_q,
		u"Failed"_q,
	};
	const auto snap = AyuFeatures::Monitor::SnapshotQueue();
	const auto activeTile = u"%1 / %2 · queue %3"_q
		.arg(snap.active)
		.arg(AyuFeatures::Monitor::kMaxConcurrent)
		.arg(snap.queued);
	const auto values = {
		activeTile,
		_todayTile,
		_totalTile,
		_failedTile,
	};
	const auto activePaths = std::set<QString>(
		snap.activePaths.begin(),
		snap.activePaths.end());
	auto captionIt = captions.begin();
	auto valueIt = values.begin();
	const auto valueH = st::semiboldFont->height;
	const auto captionH = st::normalFont->height;
	const auto tileGap = style::ConvertScale(4);
	const auto tileTop = (TilesHeight() - valueH - tileGap - captionH) / 2;
	const auto tileBlock = valueH + tileGap + captionH;
	for (auto i = 0; i != 4; ++i, ++captionIt, ++valueIt) {
		const auto left = i * tileWidth;
		p.setFont(st::semiboldFont);
		if (i == 0 && snap.active > 0) {
			p.setPen(st::windowActiveTextFg);
		} else {
			p.setPen(st::windowFg);
		}
		p.drawText(
			QRect(left, tileTop, tileWidth, valueH),
			style::al_top | style::al_center,
			*valueIt);
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(
				left,
				tileTop + valueH + tileGap,
				tileWidth,
				captionH),
			style::al_top | style::al_center,
			*captionIt);
		if (i > 0) {
			p.fillRect(left, tileTop + 2, 1, tileBlock - 4, st::shadowFg);
		}
	}
	p.fillRect(0, TilesHeight() - 1, w, 1, st::shadowFg);

	// Filter chips.
	const auto chips = std::vector<QString>{
		u"Target: "_q + _targetOptions[_targetFilter].second,
		u"Type: "_q + _typeOptions[_typeFilter],
		u"Status: "_q + _statusOptions[_statusFilter],
	};
	const auto metrics = QFontMetrics(st::normalFont);
	const auto chipY = TilesHeight() + (FiltersHeight() - ChipHeight()) / 2;
	auto chipLeft = style::ConvertScale(8);
	for (const auto &chip : chips) {
		const auto chipWidth = metrics.horizontalAdvance(chip) + 24;
		{
			const auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			p.drawRoundedRect(chipLeft, chipY, chipWidth, ChipHeight(), 8, 8);
		}
		p.setPen(st::windowFg);
		p.drawText(
			QRect(chipLeft, chipY, chipWidth, ChipHeight()),
			style::al_center,
			chip);
		chipLeft += chipWidth + 10;
	}
	p.fillRect(0, TilesHeight() + FiltersHeight() - 1, w, 1, st::shadowFg);
	// Destructive "Clear history" action, right-aligned in the filter row.
	const auto clearText = u"Clear history"_q;
	const auto clearLeft = w - RightMargin()
		- metrics.horizontalAdvance(clearText);
	p.setFont(st::normalFont);
	p.setPen(st::boxTextFgError);
	p.drawText(
		QRect(clearLeft, chipY, w - RightMargin() - clearLeft, ChipHeight()),
		style::al_left | style::al_center,
		clearText);

	// Groups.
	const auto listTop = TilesHeight() + FiltersHeight();
	auto y = listTop;
	if (_groups.empty()) {
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(0, y, w, height() - y),
			style::al_center,
			_loading ? u"Loading..."_q : u"No downloads yet"_q);
		return;
	}
	const auto versionMetrics = QFontMetrics(st::normalFont);
	const auto listLeft = style::ConvertScale(8);
	for (const auto &group : _groups) {
		p.setFont(st::semiboldFont);
		p.setPen(st::windowFg);
		p.drawText(
			listLeft,
			group.top + GroupHeaderHeight() / 2 + st::semiboldFont->height / 2
				- st::semiboldFont->descent,
			group.header);
		if (group.rows.size() > 1) {
			p.setFont(st::normalFont);
			p.setPen(st::windowSubTextFg);
			const auto count = u"%1 versions"_q.arg(group.rows.size());
			p.drawText(
				w - RightMargin() - versionMetrics.horizontalAdvance(count),
				group.top + GroupHeaderHeight() / 2 + st::normalFont->height / 2
					- st::normalFont->descent,
				count);
		}

		auto rowY = group.top + GroupHeaderHeight();
		for (const auto &row : group.rows) {
			const auto downloading = activePaths.contains(row.path);
			const auto status = downloading ? u"Downloading"_q : row.status;
			const auto statusColor = downloading
				? int(kStatusPending)
				: row.statusColor;
			const auto dotColor = (statusColor == kStatusDone)
				? st::windowActiveTextFg
				: (statusColor == kStatusFailed)
				? st::boxTextFgError
				: st::windowSubTextFg;
			p.setPen(Qt::NoPen);
			p.setBrush(dotColor);
			p.drawEllipse(
				style::ConvertScale(12),
				rowY + (VersionHeight() - style::ConvertScale(3)) / 2,
				style::ConvertScale(3),
				style::ConvertScale(3));

			p.setFont(st::normalFont);
			const auto meta = row.meta + (downloading ? u" ·  +1"_q : QString());
			const auto statusWidth = versionMetrics.horizontalAdvance(status);
			const auto metaWidth = versionMetrics.horizontalAdvance(meta);
			p.setPen(st::windowFg);
			const auto nameLeft = style::ConvertScale(20);
			const auto textGap = style::ConvertScale(6);
			const auto nameWidth = w - nameLeft - RightMargin()
				- statusWidth - textGap - metaWidth - textGap;
			const auto elidedName = (versionMetrics.horizontalAdvance(row.name) > nameWidth)
				? versionMetrics.elidedText(row.name, Qt::ElideMiddle, nameWidth)
				: row.name;
			p.drawText(
				nameLeft,
				rowY + VersionHeight() / 2 + st::normalFont->height / 2
					- st::normalFont->descent,
				elidedName);
			p.setPen(st::windowSubTextFg);
			p.drawText(
				w - RightMargin() - statusWidth - textGap - metaWidth,
				rowY + VersionHeight() / 2 + st::normalFont->height / 2
					- st::normalFont->descent,
				meta);
			p.setPen(downloading ? st::windowActiveTextFg : p.pen());
			p.drawText(
				w - RightMargin() - statusWidth,
				rowY + VersionHeight() / 2 + st::normalFont->height / 2
					- st::normalFont->descent,
				status);
			rowY += VersionHeight();
		}

		p.fillRect(
			listLeft,
			group.top + group.height - GroupPad() / 2 - 1,
			w - listLeft - RightMargin(),
			1,
			st::shadowFg);
		y = group.top + group.height;
	}

	if (!_endReached) {
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			QRect(0, y, w, GroupHeaderHeight()),
			style::al_center,
			u"Loading more..."_q);
	}
}

std::optional<int> ActivityView::hitVersionRow(QPoint pos) const {
	if (pos.y() < TilesHeight() + FiltersHeight()) {
		return std::nullopt;
	}
	for (auto g = 0; g != int(_groups.size()); ++g) {
		const auto &group = _groups[g];
		if (pos.y() < group.top) {
			continue;
		}
		if (pos.y() >= group.top + group.height) {
			continue;
		}
		const auto raw = pos.y() - group.top - GroupHeaderHeight();
		if (raw < 0) {
			// The click landed on the group header.
			return std::nullopt;
		}
		const auto index = raw / VersionHeight();
		if (index < int(group.rows.size())) {
			return g * 1000 + index;
		}
		return std::nullopt;
	}
	return std::nullopt;
}

void ActivityView::showFileMenu(QPoint globalPos, const VersionRow &row) {
	if (!row.done || !QFileInfo::exists(row.path)) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	_menu->addAction(u"Open"_q, [=] {
		QDesktopServices::openUrl(QUrl::fromLocalFile(row.path));
	});
	_menu->addAction(
		tr::lng_context_show_in_folder(tr::now),
		[=] {
			File::ShowInFolder(row.path);
		});
	_menu->popup(globalPos);
}

void ActivityView::showFilterMenu(int chipIndex, QPoint globalPos) {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	auto &menu = _menu;
	const auto select = [=, this](int index) {
		if (chipIndex == 0) {
			_targetFilter = index;
		} else if (chipIndex == 1) {
			_typeFilter = index;
		} else {
			_statusFilter = index;
		}
		_groups.clear();
		_groupedMessages.clear();
		_oldestFakeId = 0;
		_endReached = false;
		loadPage();
	};
	if (chipIndex == 0) {
		for (auto i = 0; i != int(_targetOptions.size()); ++i) {
			const auto index = i;
			menu->addAction(_targetOptions[i].second, [=] {
				select(index);
			});
		}
	} else {
		const auto &options = (chipIndex == 1) ? _typeOptions : _statusOptions;
		for (auto i = 0; i != int(options.size()); ++i) {
			const auto index = i;
			menu->addAction(options[i], [=] {
				select(index);
			});
		}
	}
	menu->popup(globalPos);
}

void ActivityView::resetHistory() {
	_groups.clear();
	_groupedMessages.clear();
	_oldestFakeId = 0;
	_endReached = false;
	refreshStats();
	loadPage();
}

void ActivityView::clearHistory() {
	const auto session = &_controller->session();
	const auto userId = session->userId().bare & PeerId::kChatTypeMask;
	// Guard against view destruction before the callback runs.
	const auto guard = QPointer<ActivityView>(this);
	ConfirmOverlay::Show(
		window(),
		u"Clear history?"_q,
		u"All download records will be removed from the list.\n\nFiles on disk and the event log are not affected."_q,
		u"Clear"_q,
		[=] {
			AyuDatabase::Monitor::clearMonitorFiles(userId);
			if (guard) {
				guard->resetHistory();
			}
		});
}

void ActivityView::mousePressEvent(QMouseEvent *e) {
	const auto pos = e->pos();
	const auto globalPos = e->globalPos();
	const auto chipY = TilesHeight() + (FiltersHeight() - ChipHeight()) / 2;
	if (pos.y() < TilesHeight()) {
		// The failed tile filters the list to failed entries.
		if (_failedCount > 0 && pos.x() >= (width() / 4) * 3) {
			_statusFilter = 3;
			_groups.clear();
			_groupedMessages.clear();
			_oldestFakeId = 0;
			_endReached = false;
			loadPage();
		}
		return;
	}
	if (pos.y() >= chipY && pos.y() < chipY + ChipHeight()) {
		const auto metrics = QFontMetrics(st::normalFont);
		const auto chips = std::vector<QString>{
			u"Target: "_q + _targetOptions[_targetFilter].second,
			u"Type: "_q + _typeOptions[_typeFilter],
			u"Status: "_q + _statusOptions[_statusFilter],
		};
		auto chipLeft = 16;
		for (auto i = 0; i != int(chips.size()); ++i) {
			const auto width = metrics.horizontalAdvance(chips[i]) + 24;
			if (pos.x() >= chipLeft && pos.x() < chipLeft + width) {
				showFilterMenu(i, globalPos);
				return;
			}
			chipLeft += width + 10;
		}
		const auto clearText = u"Clear history"_q;
		const auto clearLeft = width() - RightMargin()
			- metrics.horizontalAdvance(clearText);
		if (pos.x() >= clearLeft - 8 && pos.x() < width() - 8) {
			clearHistory();
			return;
		}
		return;
	}
	const auto hit = hitVersionRow(pos);
	if (hit.has_value()) {
		const auto groupIndex = *hit / 1000;
		const auto rowIndex = *hit % 1000;
		showFileMenu(globalPos, _groups[groupIndex].rows[rowIndex]);
	}
}

} // namespace MonitorCenter
