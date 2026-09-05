// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/settings/settings_monitor.h"

#include "lang_auto.h"
#include "ayu/ayu_settings.h"
#include "ayu/data/ayu_database.h"
#include "ayu/data/entities.h"
#include "ayu/features/monitor/monitor.h"
#include "ayu/ui/settings/ayu_builder.h"
#include "ayu/ui/settings/settings_ayu_utils.h"
#include "ayu/ui/settings/settings_main.h"
#include "core/file_utilities.h"
#include "lang/lang_text_entity.h"
#include "main/main_session.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"

#include <QDesktopServices>
#include <QDir>
#include <QUrl>

namespace Settings {
namespace {

using namespace Builder;
using namespace AyuBuilder;

void BuildMonitorToggles(SectionBuilder &builder, AyuSectionBuilder &ayu) {
	ayu.addSettingToggle({
		.id = u"ayu/monitorEnabled"_q,
		.title = tr::ayu_MonitorEnabled(),
		.getter = &AyuSettings::monitorEnabled,
		.setter = &AyuSettings::setMonitorEnabled,
		.icon = { &st::menuIconDownload },
	});
	ayu.addSettingToggle({
		.id = u"ayu/monitorPaused"_q,
		.title = tr::ayu_MonitorPaused(),
		.getter = &AyuSettings::monitorPaused,
		.setter = &AyuSettings::setMonitorPaused,
		.icon = { &st::menuIconMute },
	});

	ayu.addSectionDivider();

	ayu.addSettingToggle({
		.id = u"ayu/monitorPhoto"_q,
		.title = tr::ayu_MonitorTypePhoto(),
		.getter = &AyuSettings::monitorDownloadPhoto,
		.setter = &AyuSettings::setMonitorDownloadPhoto,
		.icon = { &st::menuIconPhoto },
	});
	ayu.addSettingToggle({
		.id = u"ayu/monitorVideo"_q,
		.title = tr::ayu_MonitorTypeVideo(),
		.getter = &AyuSettings::monitorDownloadVideo,
		.setter = &AyuSettings::setMonitorDownloadVideo,
	});
	ayu.addSettingToggle({
		.id = u"ayu/monitorVoice"_q,
		.title = tr::ayu_MonitorTypeVoice(),
		.getter = &AyuSettings::monitorDownloadVoice,
		.setter = &AyuSettings::setMonitorDownloadVoice,
	});
	ayu.addSettingToggle({
		.id = u"ayu/monitorAudio"_q,
		.title = tr::ayu_MonitorTypeAudio(),
		.getter = &AyuSettings::monitorDownloadAudio,
		.setter = &AyuSettings::setMonitorDownloadAudio,
	});
	ayu.addSettingToggle({
		.id = u"ayu/monitorVideoNote"_q,
		.title = tr::ayu_MonitorTypeVideoNote(),
		.getter = &AyuSettings::monitorDownloadVideoNote,
		.setter = &AyuSettings::setMonitorDownloadVideoNote,
	});
	ayu.addSettingToggle({
		.id = u"ayu/monitorGif"_q,
		.title = tr::ayu_MonitorTypeGif(),
		.getter = &AyuSettings::monitorDownloadGif,
		.setter = &AyuSettings::setMonitorDownloadGif,
		.icon = { &st::menuIconGif },
	});
	ayu.addSettingToggle({
		.id = u"ayu/monitorDocument"_q,
		.title = tr::ayu_MonitorTypeDocument(),
		.getter = &AyuSettings::monitorDownloadDocument,
		.setter = &AyuSettings::setMonitorDownloadDocument,
		.icon = { &st::menuIconFile },
	});
}

void BuildMonitorPaths(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addButton({
		.id = u"ayu/monitorSaveRoot"_q,
		.title = tr::ayu_MonitorSavePath(),
		.icon = { &st::menuIconStorage },
		.onClick = [=] {
			FileDialog::GetFolder(
				{},
				tr::ayu_MonitorChoosePath(tr::now),
				QString(),
				[=](QString &&result) {
					AyuSettings::getInstance().setMonitorSaveRoot(result);
				});
		},
	});
	builder.addButton({
		.id = u"ayu/monitorOpenDownloads"_q,
		.title = tr::ayu_MonitorOpenDownloads(),
		.icon = { &st::menuIconShowInFolder },
		.onClick = [=] {
			const auto path = AyuFeatures::Monitor::ResolveSaveRoot();
			QDir().mkpath(path);
			QDesktopServices::openUrl(QUrl::fromLocalFile(path));
		},
	});
}

void BuildMonitorDanger(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addButton({
		.id = u"ayu/monitorClearTargets"_q,
		.title = tr::ayu_MonitorClearTargets(),
		.icon = { &st::menuIconDelete },
		.onClick = [=] {
			controller->show(Ui::MakeConfirmBox({
				.text = tr::ayu_MonitorClearTargetsConfirmation(tr::rich),
				.confirmed = [=](Fn<void()> &&close) {
					const auto session = &controller->session();
					const auto userId = session->userId().bare & PeerId::kChatTypeMask;
					for (const auto &target : AyuDatabase::Monitor::getAllMonitorTargets(userId)) {
						AyuDatabase::Monitor::removeMonitorTarget(
							target.userId,
							target.peerId,
							target.topicId);
					}
					close();
				},
				.confirmText = tr::lng_box_yes(),
			}));
		},
	});
	builder.addSkip();
}

const auto kMeta = BuildHelper({
	.id = AyuMonitor::Id(),
	.parentId = AyuMain::Id(),
	.title = &tr::ayu_CategoryMonitor,
	.icon = &st::menuIconDownload,
}, [](SectionBuilder &builder) {
	auto ayu = AyuSectionBuilder(builder);

	builder.addSkip();
	BuildMonitorToggles(builder, ayu);
	builder.addDivider();
	BuildMonitorPaths(builder);
	builder.addDivider();
	BuildMonitorDanger(builder);
});

} // namespace

rpl::producer<QString> AyuMonitor::title() {
	return tr::ayu_CategoryMonitor();
}

AyuMonitor::AyuMonitor(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void AyuMonitor::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

} // namespace Settings
