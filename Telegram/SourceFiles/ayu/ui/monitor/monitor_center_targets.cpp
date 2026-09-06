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
#include "ui/painter.h"
#include "ui/style/style_core_scale.h"
#include "ui/widgets/checkbox.h"
#include "window/window_session_controller.h"

#include <map>

namespace MonitorCenter {
namespace {

// Handwritten pixel constants are "design pixels" for a 13px font and go
// through style::ConvertScale (font metrics are already scaled).
[[nodiscard]] int RowHeaderHeight() {
	return st::semiboldFont->height + style::ConvertScale(11);
}
[[nodiscard]] int RowEditorLineHeight() {
	return style::ConvertScale(20);
}
[[nodiscard]] int RowEditorPad() {
	return style::ConvertScale(6);
}
[[nodiscard]] int RowRemoveHeight() {
	return st::semiboldFont->height + style::ConvertScale(15);
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
	QRect _removeRect;
	object_ptr<ToggleWidget> _toggle;

};

TargetsView::TargetsView(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Ui::RpWidget(parent)
, _controller(controller) {
	reload();
}

void TargetsView::reload() {
	_rows.clear();

	const auto session = &_controller->session();
	const auto userId = session->userId().bare & PeerId::kChatTypeMask;
	auto done = std::map<long long, int>();
	auto failed = std::map<long long, int>();
	auto bytes = std::map<long long, int64>();
	for (const auto &entry : AyuDatabase::Monitor::getTargetStats(userId)) {
		done.emplace(entry.peerId, entry.doneCount);
		failed.emplace(entry.peerId, entry.failedCount);
		bytes.emplace(entry.peerId, entry.doneBytes);
	}

	for (const auto &target : AyuDatabase::Monitor::getAllMonitorTargets(userId)) {
		auto row = object_ptr<Row>(
			this,
			_controller,
			target,
			done.contains(target.peerId) ? done[target.peerId] : 0,
			bytes.contains(target.peerId) ? bytes[target.peerId] : 0,
			failed.contains(target.peerId) ? failed[target.peerId] : 0,
			[=] { reload(); },
			[=] { relayout(); });
		_rows.push_back(std::move(row));
	}
	resizeToWidth(width());
	update();
}

void TargetsView::relayout() {
	resizeToWidth(width());
	update();
}

int TargetsView::resizeGetHeight(int newWidth) {
	auto y = style::ConvertScale(20);
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
		style::ConvertScale(13) + st::normalFont->ascent,
		u"Targets (add via the chat context menu)"_q);
	p.fillRect(0, style::ConvertScale(20), width(), 1, st::shadowFg);
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
	setMouseTracking(true);
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
	return _expanded
		? RowHeaderHeight()
			+ RowEditorPad() + st::normalFont->height + style::ConvertScale(7)
			+ kEditorRows * RowEditorLineHeight()
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
	if (_expanded) {
		const auto colWidth = (newWidth - style::ConvertScale(32)) / 2;
		auto y = RowHeaderHeight()
			+ RowEditorPad()
			+ st::normalFont->height + style::ConvertScale(7);
		for (auto i = 0; i != kTypeCount; ++i) {
			_typeChecks[i]->moveToLeft(
				style::ConvertScale(16) + (i / kEditorRows) * colWidth,
				y + (i % kEditorRows) * RowEditorLineHeight());
			_typeChecks[i]->resizeToNaturalWidth(colWidth - style::ConvertScale(6));
		}
		y += kEditorRows * RowEditorLineHeight();
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

	p.setFont(st::semiboldFont);
	p.setPen(st::windowFg);
	p.drawText(
		style::ConvertScale(8),
		RowHeaderHeight() / 2 + st::semiboldFont->height / 2
			- st::semiboldFont->descent,
		MonitorPeerName(_controller, _target.peerId));

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
					ownChanged();
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
	if (overRemove || _removeRect.contains(mapFromGlobal(QCursor::pos()))) {
		update();
	}
	Ui::RpWidget::mouseMoveEvent(e);
}

} // namespace MonitorCenter
