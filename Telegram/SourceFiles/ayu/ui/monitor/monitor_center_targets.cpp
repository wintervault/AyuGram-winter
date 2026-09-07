// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/monitor/monitor_center_targets.h"

#include "ayu/data/ayu_database.h"
#include "ayu/data/entities.h"
#include "ayu/ayu_settings.h"
#include "ayu/features/monitor/monitor.h"
#include "ayu/ui/monitor/monitor_center.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"
#include "styles/style_widgets.h"
#include "ui/painter.h"
#include "ui/style/style_core_scale.h"
#include "ui/widgets/checkbox.h"
#include "window/window_session_controller.h"

#include <algorithm>
#include <map>

namespace MonitorCenter {
namespace {

// Handwritten pixel constants are "design pixels" for a 13px font and go
// through style::ConvertScale (font metrics are already scaled).
[[nodiscard]] int RowHeaderHeight() {
	return st::semiboldFont->height + style::ConvertScale(11);
}
[[nodiscard]] int RowEditorPad() {
	return style::ConvertScale(6);
}
[[nodiscard]] int RowRemoveHeight() {
	return st::semiboldFont->height + style::ConvertScale(15);
}
[[nodiscard]] int TitleHeight() {
	// Page title strip above the first row.
	return st::normalFont->height + style::ConvertScale(14);
}

constexpr auto kTypeCount = 7;
constexpr auto kEditorRows = (kTypeCount + 1) / 2;

const std::vector<QString> &TypeLabels() {
	static const auto result = std::vector<QString>{
		u"Photo"_q,
		u"Video"_q,
		u"Voice"_q,
		u"Audio"_q,
		u"Video note"_q,
		u"GIF"_q,
		u"Document"_q,
	};
	return result;
}

const std::vector<std::string> &TypeNames() {
	static const auto result = std::vector<std::string>{
		"photo",
		"video",
		"voice",
		"audio",
		"video_note",
		"gif",
		"document",
	};
	return result;
}

[[nodiscard]] std::vector<QString> ParseTypes(const std::string &mediaTypes) {
	auto result = std::vector<QString>();
	const auto parts = QString::fromStdString(mediaTypes)
		.split(',', Qt::SkipEmptyParts);
	for (const auto &part : parts) {
		result.push_back(part.trimmed());
	}
	return result;
}

} // namespace

// A standalone switch widget built on ToggleView.
class ToggleWidget final : public Ui::RpWidget {
public:
	ToggleWidget(QWidget *parent, bool checked)
	: Ui::RpWidget(parent)
	, _view(st::defaultToggle, checked, [=] { update(); }) {
		setCursor(style::cur_pointer);
		resize(_view.getSize());
	}

	void setChecked(bool value) {
		_view.setChecked(value, anim::type::instant);
	}

	[[nodiscard]] rpl::producer<bool> checkedChanges() const {
		return _view.checkedChanges();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = QPainter(this);
		_view.paint(p, 0, 0, width());
	}

	void mousePressEvent(QMouseEvent *e) override {
		_view.setChecked(!_view.checked(), anim::type::normal);
	}

private:
	Ui::ToggleView _view;

};

class TargetsView::Row final : public Ui::RpWidget {
public:
	Row(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		MonitorTarget target,
		int doneCount,
		int64 doneBytes,
		int failedCount,
		Fn<void()> changed,
		Fn<void()> geometryChanged);

protected:
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	int resizeGetHeight(int newWidth) override;
	bool eventFilter(QObject *obj, QEvent *e) override;

private:
	void saveTypes();
	void updateChildrenGeometry(int newWidth);

	const not_null<Window::SessionController*> _controller;
	MonitorTarget _target;
	int _doneCount = 0;
	int64 _doneBytes = 0;
	int _failedCount = 0;
	Fn<void()> _changed;
	Fn<void()> _geometryChanged;

	bool _expanded = false;
	std::vector<Ui::Checkbox*> _typeChecks;
	std::vector<bool> _globalAllowed;
	int _editorLineHeight = 0;
	int _checkHeight = 0;
	QRect _removeRect;
	object_ptr<ToggleWidget> _toggle;

};

TargetsView::TargetsView(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Ui::RpWidget(parent)
, _controller(controller) {
	// Live-cursor hover for the title-strip refresh pill: repaint on
	// Leave (cursor out) and Enter (overlay dismissed above us).
	setMouseTracking(true);
	installEventFilter(this);
	reload();
}

void TargetsView::reload() {
	_rows.clear();

	const auto session = &_controller->session();
	const auto userId = session->userId().bare & PeerId::kChatTypeMask;
	// Per-topic stats for topic targets, per-peer totals for chat-level
	// ones (their scope is the whole chat, topics included).
	struct Stats {
		int done = 0;
		int64 bytes = 0;
		int failed = 0;
	};
	auto perTopic = std::map<std::pair<long long, long long>, Stats>();
	auto perPeer = std::map<long long, Stats>();
	for (const auto &entry : AyuDatabase::Monitor::getTargetStats(userId)) {
		auto &topic = perTopic[std::make_pair(entry.peerId, entry.topicId)];
		topic.done += entry.doneCount;
		topic.bytes += entry.doneBytes;
		topic.failed += entry.failedCount;
		auto &peer = perPeer[entry.peerId];
		peer.done += entry.doneCount;
		peer.bytes += entry.doneBytes;
		peer.failed += entry.failedCount;
	}
	const auto lookup = [&](const MonitorTarget &target) {
		if (target.topicId != 0) {
			const auto it = perTopic.find(
				std::make_pair(target.peerId, target.topicId));
			return (it != perTopic.end())
				? it->second
				: Stats{};
		}
		const auto it = perPeer.find(target.peerId);
		return (it != perPeer.end()) ? it->second : Stats{};
	};

	for (const auto &target : AyuDatabase::Monitor::getAllMonitorTargets(userId)) {
		const auto stats = lookup(target);
		auto row = object_ptr<Row>(
			this,
			_controller,
			target,
			stats.done,
			stats.bytes,
			stats.failed,
			[=, guard = QPointer<TargetsView>(this)] {
				// Overlay confirm callbacks may run after this view was
				// destroyed by a view switch.
				if (guard) {
					guard->reload();
				}
			},
			[=] { relayout(); });
		_rows.push_back(std::move(row));
	}
	resizeToWidth(width());
	// Repaint every row explicitly as well: a plain update() on the
	// parent alone can lose child paint dispatches when rows were
	// deleted, recreated and repositioned in the same event-loop round,
	// which used to leave stale fragments with dead click areas.
	for (const auto &row : _rows) {
		row->update();
	}
	update();
}

void TargetsView::relayout() {
	resizeToWidth(width());
	update();
}

int TargetsView::resizeGetHeight(int newWidth) {
	auto y = TitleHeight();
	for (const auto &row : _rows) {
		row->resizeToWidth(newWidth);
		row->setGeometry(0, y, newWidth, row->height());
		y += row->height();
	}
	return y + style::ConvertScale(8);
}

void TargetsView::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(e->rect(), st::boxBg);

	if (_rows.empty()) {
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(
			rect().marginsRemoved(QMargins(
				style::ConvertScale(12),
				style::ConvertScale(12),
				style::ConvertScale(12),
				style::ConvertScale(12))),
			style::al_center | Qt::TextWordWrap,
			u"No monitored chats yet.\nUse the chat context menu to add one."_q);
		return;
	}
	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(
		style::ConvertScale(8),
		TitleHeight() - st::normalFont->descent - style::ConvertScale(6),
		u"Targets (add via the chat context menu)"_q);

	// "Refresh" pill, right-aligned in the title strip: force-reloads
	// the target list and its per-target stats.
	const auto refreshText = u"Refresh"_q;
	const auto metrics = QFontMetrics(st::normalFont);
	const auto refreshWidth = metrics.horizontalAdvance(refreshText)
		+ 2 * style::ConvertScale(10);
	const auto refreshHeight = st::normalFont->height
		+ 2 * style::ConvertScale(6);
	_refreshRect = QRect(
		width() - style::ConvertScale(16) - refreshWidth,
		(TitleHeight() - refreshHeight) / 2,
		refreshWidth,
		refreshHeight);
	_refreshHovered = _refreshRect.contains(mapFromGlobal(QCursor::pos()));
	if (_refreshHovered) {
		const auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowActiveTextFg);
		p.setOpacity(0.1);
		p.drawRoundedRect(
			_refreshRect,
			style::ConvertScale(5),
			style::ConvertScale(5));
		p.setOpacity(1.0);
	}
	p.drawText(_refreshRect, style::al_center, refreshText);

	p.fillRect(
		0,
		TitleHeight() - 1,
		width(),
		1,
		st::shadowFg);
}

void TargetsView::mousePressEvent(QMouseEvent *e) {
	if (_refreshRect.contains(e->pos())) {
		reload();
		return;
	}
	Ui::RpWidget::mousePressEvent(e);
}

void TargetsView::mouseMoveEvent(QMouseEvent *e) {
	const auto over = _refreshRect.contains(e->pos());
	if (over != _refreshHovered) {
		_refreshHovered = over;
		update();
	}
	setCursor(over ? style::cur_pointer : style::cur_default);
	Ui::RpWidget::mouseMoveEvent(e);
}

bool TargetsView::eventFilter(QObject *obj, QEvent *e) {
	if (obj == this
		&& (e->type() == QEvent::Leave || e->type() == QEvent::Enter)) {
		// Hover follows the live cursor in paintEvent; a Leave/Enter
		// has to trigger the repaint itself.
		update();
	}
	return Ui::RpWidget::eventFilter(obj, e);
}

TargetsView::Row::Row(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	MonitorTarget target,
	int doneCount,
	int64 doneBytes,
	int failedCount,
	Fn<void()> changed,
	Fn<void()> geometryChanged)
: Ui::RpWidget(parent)
, _controller(controller)
, _target(std::move(target))
, _doneCount(doneCount)
, _doneBytes(doneBytes)
, _failedCount(failedCount)
, _changed(std::move(changed))
, _geometryChanged(std::move(geometryChanged))
, _toggle(this, _target.enabled) {
	_toggle->setChecked(_target.enabled);
	_toggle->checkedChanges(
	) | rpl::on_next([=](bool checked) {
		_target.enabled = checked;
		AyuDatabase::Monitor::upsertMonitorTarget(_target);
		AyuFeatures::Monitor::InvalidateTargetsCache();
	}, _toggle->lifetime());

	const auto &settings = AyuSettings::getInstance();
	const auto globalAllowed = std::vector<bool>{
		settings.monitorDownloadPhoto(),
		settings.monitorDownloadVideo(),
		settings.monitorDownloadVoice(),
		settings.monitorDownloadAudio(),
		settings.monitorDownloadVideoNote(),
		settings.monitorDownloadGif(),
		settings.monitorDownloadDocument(),
	};
	_globalAllowed = globalAllowed;
	const auto types = ParseTypes(_target.mediaTypes);
	const auto empty = _target.mediaTypes.empty();
	for (auto i = 0; i != kTypeCount; ++i) {
		const auto allowed = globalAllowed[i];
		const auto checked = allowed
			&& (empty
				|| (std::find(
						types.begin(),
						types.end(),
						QString::fromStdString(TypeNames()[i]))
					!= types.end()));
		auto label = TypeLabels()[i]
			+ (allowed ? QString() : u" (off globally)"_q);
		auto check = object_ptr<Ui::Checkbox>(
			this,
			label,
			checked,
			st::defaultCheckbox);
		const auto raw = check.data();
		if (!allowed) {
			// Locked by the global toggles: shown gray, not editable.
			raw->setEnabled(false);
		}
		raw->checkedChanges(
		) | rpl::on_next([=](bool) {
			saveTypes();
		}, raw->lifetime());
		_typeChecks.push_back(raw);
	}
	// Editor controls live in updateChildrenGeometry(); keep them out of
	// sight until the first layout actually places them.
	for (const auto check : _typeChecks) {
		check->hide();
	}
	// The remove-hover highlight is computed from the live cursor
	// position, so a Leave event has to trigger a repaint or the last
	// hovered state stays painted after the cursor moves away. Child
	// widgets are filtered too: a fast move into a checkbox is
	// redirected to the child, and neither a Row move nor a Leave fires.
	installEventFilter(this);
	for (const auto check : _typeChecks) {
		check->installEventFilter(this);
	}
	_toggle->installEventFilter(this);
	setMouseTracking(true);
}

bool TargetsView::Row::eventFilter(QObject *obj, QEvent *e) {
	const auto type = e->type();
	if ((type == QEvent::Leave || type == QEvent::Enter)
		&& (obj == this || obj == _toggle.data()
			|| std::find(
				_typeChecks.begin(),
				_typeChecks.end(),
				static_cast<Ui::Checkbox*>(obj)) != _typeChecks.end())) {
		update();
	}
	return Ui::RpWidget::eventFilter(obj, e);
}

void TargetsView::Row::saveTypes() {
	auto kept = std::vector<std::string>();
	auto all = true;
	for (auto i = 0; i != kTypeCount; ++i) {
		if (!_globalAllowed[i]) {
			// Locked by the global toggles: never part of the whitelist.
			continue;
		}
		if (_typeChecks[i]->checked()) {
			kept.push_back(TypeNames()[i]);
		} else {
			all = false;
		}
	}
	if (all) {
		// Everything allowed = follow the global settings.
		_target.mediaTypes.clear();
	} else if (kept.empty()) {
		// Explicit "allow nothing": an empty whitelist would mean
		// "follow the global settings" (i.e. allow all).
		_target.mediaTypes = "none";
	} else {
		auto joined = std::string();
		for (auto i = 0; i != int(kept.size()); ++i) {
			if (i) {
				joined += ',';
			}
			joined += kept[i];
		}
		_target.mediaTypes = joined;
	}
	AyuDatabase::Monitor::upsertMonitorTarget(_target);
	AyuFeatures::Monitor::InvalidateTargetsCache();
}

int TargetsView::Row::resizeGetHeight(int newWidth) {
	updateChildrenGeometry(newWidth);
	const auto centering = (_checkHeight - _editorLineHeight) / 2;
	return _expanded
		? RowHeaderHeight()
			+ RowEditorPad() + st::normalFont->height + style::ConvertScale(7)
			+ style::ConvertScale(8) + centering
			+ kEditorRows * _editorLineHeight
			+ style::ConvertScale(8) + centering
			+ RowEditorPad() + RowRemoveHeight()
		: RowHeaderHeight();
}

void TargetsView::Row::updateChildrenGeometry(int newWidth) {
	_toggle->move(
		newWidth - style::ConvertScale(16) - _toggle->width(),
		(RowHeaderHeight() - _toggle->height()) / 2);
	for (const auto check : _typeChecks) {
		check->setVisible(_expanded);
	}
	// Checkbox widgets are tall because their style reserves a big
	// transparent ripple padding around the visible check (widget height
	// ~76 at 2x vs the 44 check itself). Base the line height on the
	// visible check diameter plus breathing room, not on the widget
	// height, otherwise each row shows a large empty gap.
	_editorLineHeight = std::max(
		st::defaultCheck.diameter + style::ConvertScale(10),
		st::normalFont->height + style::ConvertScale(10));
	if (_expanded) {
		_checkHeight = _typeChecks[0]->height();
		const auto lineHeight = _editorLineHeight;
		// The first checkbox row starts after a fixed gap below the
		// hint line, compensated for the negative centering offset of
		// the tall checkbox widgets; otherwise the labels touch the
		// hint text. The same compensation is applied after the last
		// row so its transparent bottom padding keeps clear of the
		// remove action.
		const auto centering = (_checkHeight - lineHeight) / 2;
		const auto colWidth = (newWidth - style::ConvertScale(32)) / 2;
		auto y = RowHeaderHeight()
			+ RowEditorPad()
			+ st::normalFont->height + style::ConvertScale(7)
			+ style::ConvertScale(8) + centering;
		for (auto i = 0; i != kTypeCount; ++i) {
			_typeChecks[i]->moveToLeft(
				style::ConvertScale(16) + (i / kEditorRows) * colWidth,
				y + (i % kEditorRows) * lineHeight
					+ (lineHeight - _checkHeight) / 2);
			_typeChecks[i]->resizeToNaturalWidth(colWidth - style::ConvertScale(6));
		}
		y += kEditorRows * lineHeight;
		y += style::ConvertScale(8) + centering;
		y += RowEditorPad();
		// Self-drawn destructive action, centered; no background fill so
		// the row separator under it stays uninterrupted.
		const auto removeWidth = st::semiboldFont->width(u"Remove target"_q)
			+ style::ConvertScale(24);
		const auto removeHeight = st::semiboldFont->height
			+ style::ConvertScale(7);
		_removeRect = QRect(
			(newWidth - removeWidth) / 2,
			y + (RowRemoveHeight() - removeHeight) / 2,
			removeWidth,
			removeHeight);
	}
}

void TargetsView::Row::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	const auto w = width();

	// Paint the background: the row must be self-sufficient. A fully
	// transparent row used to show blank patches after a remove
	// confirmation reloaded the list in the same event-loop round as
	// the confirm overlay's pending destruction.
	p.fillRect(rect(), st::boxBg);

	p.setFont(st::semiboldFont);
	p.setPen(st::windowFg);
	p.drawText(
		style::ConvertScale(8),
		RowHeaderHeight() / 2 + st::semiboldFont->height / 2
			- st::semiboldFont->descent,
		MonitorTargetName(_controller, _target.peerId, _target.topicId));

	p.setFont(st::normalFont);
	p.setPen(_failedCount > 0 ? st::boxTextFgError : st::windowSubTextFg);
	const auto stats = u"%1 · %2"_q
		.arg(_doneCount)
		.arg(MonitorFormatBytes(_doneBytes));
	const auto statsRight = w - style::ConvertScale(16)
		- _toggle->width() - style::ConvertScale(6);
	p.drawText(
		statsRight - QFontMetrics(st::normalFont).horizontalAdvance(stats),
		RowHeaderHeight() / 2 + st::normalFont->height / 2
			- st::normalFont->descent,
		stats);

	if (_expanded) {
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		const auto hint = u"Only selected types are downloaded; grayed-out types are disabled globally."_q;
		p.drawText(
			style::ConvertScale(12),
			RowHeaderHeight() + RowEditorPad() + st::normalFont->height
				- style::ConvertScale(2),
			hint);

		const auto removeHover = _removeRect.contains(
			mapFromGlobal(QCursor::pos()));
		if (removeHover) {
			const auto hq = PainterHighQualityEnabler(p);
			p.setPen(Qt::NoPen);
			p.setBrush(st::boxTextFgError);
			p.setOpacity(0.1);
			p.drawRoundedRect(
				_removeRect,
				style::ConvertScale(4),
				style::ConvertScale(4));
			p.setOpacity(1.0);
		}
		p.setFont(st::semiboldFont);
		p.setPen(st::boxTextFgError);
		p.drawText(_removeRect, style::al_center, u"Remove target"_q);
	}
	p.fillRect(0, RowHeaderHeight() - 1, w, 1, st::shadowFg);
	if (_expanded) {
		p.fillRect(0, height() - 1, w, 1, st::shadowFg);
	}
}

void TargetsView::Row::mousePressEvent(QMouseEvent *e) {
	const auto pos = e->pos();
	// Only the header toggles expansion; clicks in the editor area are
	// left to its own controls, except the remove action.
	if (pos.y() >= RowHeaderHeight()) {
		if (_expanded && _removeRect.contains(pos)) {
			const auto ownTarget = _target;
			const auto ownChanged = _changed;
			ConfirmOverlay::Show(
				window(),
				u"Stop monitoring this chat?"_q,
				u"The chat will be removed from the monitor list.\n\nDownloaded files are not affected."_q,
				u"Remove"_q,
				[=] {
					AyuDatabase::Monitor::removeMonitorTarget(
						ownTarget.userId,
						ownTarget.peerId,
						ownTarget.topicId);
					AyuFeatures::Monitor::InvalidateTargetsCache();
					// Reload on a later event-loop round: the confirm
					// overlay is still pending destruction (deleteLater)
					// in this round, and interleaving its teardown with
					// the row rebuild produced stale blank patches.
					crl::on_main(ownChanged);
				});
		}
		return;
	}
	_expanded = !_expanded;
	resizeToWidth(width());
	update();
	_geometryChanged();
}

void TargetsView::Row::mouseMoveEvent(QMouseEvent *e) {
	const auto overRemove = _expanded
		&& _removeRect.contains(e->pos());
	setCursor(overRemove ? style::cur_pointer : style::cur_default);
	// Repaint unconditionally: the hover state is derived from the live
	// cursor in paintEvent, and leaving the remove rect inside this row
	// must clear the highlight even though no Leave event fires.
	update();
	Ui::RpWidget::mouseMoveEvent(e);
}

} // namespace MonitorCenter
