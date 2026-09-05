// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/monitor/monitor.h"

#include "ayu/ayu_settings.h"
#include "ayu/data/ayu_database.h"
#include "ayu/data/entities.h"
#include "ayu/features/monitor/monitor_downloader.h"
#include "ayu/features/monitor/monitor_queue.h"
#include "ayu/utils/telegram_helpers.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStorageInfo>

namespace AyuFeatures::Monitor {
namespace {

constexpr auto kEventLogLimit = 500;

enum class MediaType {
	photo,
	video,
	voice,
	audio,
	videoNote,
	gif,
	document,
	other,
};

} // namespace

QString ResolveSaveRoot() {
	const auto &settings = AyuSettings::getInstance();
	auto root = settings.monitorSaveRoot();
	if (root.isEmpty()) {
		root = cWorkingDir() + u"monitor_downloads"_q;
	}
	if (!root.endsWith('/')) {
		root += '/';
	}
	return root;
}

QString DefaultNameTemplate() {
	return u"{chat_title}\\{yyyy-MM-dd}\\{msg_id}_{orig_name}"_q;
}

namespace {

struct PendingMedia {
	not_null<HistoryItem*> item;
	DocumentData *document = nullptr;
	PhotoData *photo = nullptr;
	MediaType type = MediaType::other;
	int64 mediaId = 0;
};

[[nodiscard]] QString SanitizeNamePart(QString part) {
	// Braces are stripped so that substituted values can never inject
	// template placeholders of their own.
	static const auto invalid = QRegularExpression(u"[\\\\/:*?\"<>|{}]+"_q);
	return part.replace(invalid, u"_"_q).trimmed();
}

[[nodiscard]] MediaType ClassifyDocument(not_null<DocumentData*> document) {
	if (document->sticker()) {
		return MediaType::other;
	} else if (document->isVoiceMessage()) {
		return MediaType::voice;
	} else if (document->isVideoMessage()) {
		return MediaType::videoNote;
	} else if (document->isAnimation()) {
		return MediaType::gif;
	} else if (document->isVideoFile()) {
		return MediaType::video;
	} else if (document->isAudioFile()) {
		return MediaType::audio;
	}
	return MediaType::document;
}

[[nodiscard]] bool TypeAllowed(MediaType type) {
	const auto &settings = AyuSettings::getInstance();
	switch (type) {
	case MediaType::photo: return settings.monitorDownloadPhoto();
	case MediaType::video: return settings.monitorDownloadVideo();
	case MediaType::voice: return settings.monitorDownloadVoice();
	case MediaType::audio: return settings.monitorDownloadAudio();
	case MediaType::videoNote: return settings.monitorDownloadVideoNote();
	case MediaType::gif: return settings.monitorDownloadGif();
	case MediaType::document: return settings.monitorDownloadDocument();
	case MediaType::other: return false;
	}
	return false;
}

[[nodiscard]] QString TypeName(MediaType type) {
	switch (type) {
	case MediaType::photo: return u"photo"_q;
	case MediaType::video: return u"video"_q;
	case MediaType::voice: return u"voice"_q;
	case MediaType::audio: return u"audio"_q;
	case MediaType::videoNote: return u"video_note"_q;
	case MediaType::gif: return u"gif"_q;
	case MediaType::document: return u"document"_q;
	case MediaType::other: return u"other"_q;
	}
	return u"other"_q;
}

[[nodiscard]] std::vector<MonitorTarget> GetTargetsCached(ID userId) {
	struct Cache {
		ID userId = 0;
		std::vector<MonitorTarget> targets;
		crl::time fetched = 0;
	};
	static Cache cache;
	constexpr auto kTtl = 5 * crl::time(1000);
	const auto now = crl::now();
	if (cache.userId == userId && now - cache.fetched < kTtl) {
		return cache.targets;
	}
	cache.targets = AyuDatabase::Monitor::getAllMonitorTargets(userId);
	cache.userId = userId;
	cache.fetched = now;
	return cache.targets;
}

[[nodiscard]] bool IsMonitored(not_null<HistoryItem*> item) {
	const auto &settings = AyuSettings::getInstance();
	if (!settings.monitorEnabled() || settings.monitorPaused()) {
		return false;
	}
	if (item->isLocal() || item->isService() || item->isEmpty()) {
		return false;
	}
	const auto session = &item->history()->session();
	const auto userId = session->userId().bare & PeerId::kChatTypeMask;
	const auto peerId = item->history()->peer->id.value & PeerId::kChatTypeMask;
	const auto topicId = item->topicRootId().bare;
	const auto targets = GetTargetsCached(userId);
	for (const auto &target : targets) {
		if (!target.enabled) {
			continue;
		}
		if (target.peerId != peerId) {
			continue;
		}
		if (target.topicId != 0 && target.topicId != topicId) {
			continue;
		}
		return true;
	}
	return false;
}

void AppendEvent(
		ID userId,
		int level,
		const QString &category,
		ID peerId,
		int messageId,
		const QString &text) {
	auto event = MonitorEvent();
	event.userId = userId;
	event.date = base::unixtime::now();
	event.level = level;
	event.category = category.toStdString();
	event.peerId = peerId;
	event.messageId = messageId;
	event.text = text.toStdString();
	AyuDatabase::Monitor::addMonitorEvent(event);
}

[[nodiscard]] std::optional<PendingMedia> ExtractMedia(
		not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return std::nullopt;
	}
	auto result = PendingMedia{ .item = item };
	if (const auto photo = media->photo()) {
		result.photo = photo;
		result.type = MediaType::photo;
		result.mediaId = photo->id;
		return result;
	} else if (const auto document = media->document()) {
		if (document->sticker()) {
			return std::nullopt;
		}
		result.document = document;
		result.type = ClassifyDocument(document);
		result.mediaId = document->id;
		return result;
	}
	return std::nullopt;
}

[[nodiscard]] QString BuildFilePath(not_null<HistoryItem*> item, int version) {
	const auto &settings = AyuSettings::getInstance();
	auto templ = settings.monitorNameTemplate();
	if (templ.trimmed().isEmpty()) {
		templ = DefaultNameTemplate();
	}
	templ.replace('\\', '/');

	const auto pending = ExtractMedia(item);
	if (!pending.has_value()) {
		return QString();
	}
	const auto &media = *pending;

	auto origName = QString();
	if (media.document) {
		origName = media.document->filename();
		if (origName.isEmpty()) {
			origName = u"file_%1"_q.arg(media.mediaId);
		}
	} else {
		origName = u"photo_%1.jpg"_q.arg(media.mediaId);
	}
	const auto origInfo = QFileInfo(origName);
	const auto date = base::unixtime::parse(item->date()).date().toString(u"yyyy-MM-dd"_q);

	auto relative = std::move(templ);
	const auto vars = std::vector<std::pair<QString, QString>>{
		{ u"chat_title"_q, SanitizeNamePart(item->history()->peer->name()) },
		{ u"chat_id"_q, QString::number(item->history()->peer->id.value & PeerId::kChatTypeMask) },
		{ u"topic_id"_q, QString::number(item->topicRootId().bare) },
		{ u"msg_id"_q, QString::number(item->id.bare) },
		{ u"media_id"_q, QString::number(media.mediaId) },
		{ u"type"_q, TypeName(media.type) },
		{ u"ext"_q, origInfo.suffix() },
		{ u"orig_name"_q, SanitizeNamePart(origName) },
		{ u"date"_q, date },
		{ u"yyyy-MM-dd"_q, date },
	};
	for (const auto &[key, value] : vars) {
		relative.replace('{' + key + '}', value);
	}

	// Split into segments, dropping traversal and empty ones.
	auto segments = relative.split('/', Qt::SkipEmptyParts);
	for (auto &segment : segments) {
		segment = SanitizeNamePart(segment);
	}
	segments.removeAll(u"."_q);
	segments.removeAll(u".."_q);
	if (segments.isEmpty()) {
		return QString();
	}

	// Version suffix goes before the final file name extension.
	if (version > 1) {
		const auto info = QFileInfo(segments.back());
		const auto base = info.completeBaseName();
		if (base.isEmpty()) {
			segments.back() = info.fileName() + u"_v%1"_q.arg(version);
		} else {
			segments.back() = base
				+ u"_v%1"_q.arg(version)
				+ (info.suffix().isEmpty() ? QString() : u'.' + info.suffix());
		}
	}

	return ResolveSaveRoot() + segments.join('/');
}

void EnsureMediaDownloaded(not_null<HistoryItem*> item) {
	if (!IsMonitored(item)) {
		return;
	}
	const auto pending = ExtractMedia(item);
	if (!pending.has_value()) {
		return;
	}
	const auto &media = *pending;
	const auto &settings = AyuSettings::getInstance();
	const auto session = &item->history()->session();
	const auto userId = session->userId().bare & PeerId::kChatTypeMask;
	const auto peerId = item->history()->peer->id.value & PeerId::kChatTypeMask;
	const auto msgId = item->id.bare;
	const auto typeName = TypeName(media.type).toStdString();
	const auto origin = item->fullId();

	if (!TypeAllowed(media.type)) {
		AppendEvent(userId, 0, u"skip"_q, peerId, msgId, u"type disabled: %1"_q.arg(TypeName(media.type)));
		return;
	}

	auto photoSize = std::optional<Data::PhotoSize>();
	if (media.photo) {
		photoSize = ResolveBestPhotoSize(media.photo);
		if (!photoSize) {
			AppendEvent(userId, 2, u"error"_q, peerId, msgId, u"no valid photo size"_q);
			return;
		}
	}

	const auto expectedSize = media.document
		? media.document->size
		: media.photo->imageByteSize(*photoSize);
	const auto maxSize = int64(settings.monitorMaxFileSizeMB()) * 1024 * 1024;
	if (maxSize > 0 && expectedSize > maxSize) {
		AppendEvent(userId, 1, u"skip"_q, peerId, msgId, u"oversize: %1"_q.arg(expectedSize));
		return;
	}

	const auto minFreeBytes = int64(settings.monitorMinDiskSpaceMB()) * 1024 * 1024;
	if (minFreeBytes > 0) {
		const auto storage = QStorageInfo(ResolveSaveRoot());
		const auto available = storage.bytesAvailable();
		if (storage.isValid() && available >= 0 && available < minFreeBytes) {
			AppendEvent(
				userId,
				1,
				u"skip"_q,
				peerId,
				msgId,
				u"low disk space: %1 MB free"_q.arg(available / (1024 * 1024)));
			return;
		}
	}

	auto row = std::optional<MonitorFile>();
	if (auto existing = AyuDatabase::Monitor::getMonitorFile(userId, media.mediaId, typeName)) {
		const auto status = existing->status;
		const auto fileGone = (status == int(MonitorFileStatus::done))
			&& !QFileInfo::exists(QString::fromStdString(existing->filePath));
		if (status != int(MonitorFileStatus::failed) && !fileGone) {
			return;
		}
		existing->status = int(MonitorFileStatus::pending);
		existing->errorInfo.clear();
		existing->downloadedDate = base::unixtime::now();
		AyuDatabase::Monitor::updateMonitorFile(*existing);
		row = std::move(*existing);
	} else {
		const auto latestVersion = AyuDatabase::Monitor::getLatestFileVersion(userId, peerId, msgId);
		if (!latestVersion.has_value()) {
			AppendEvent(userId, 2, u"error"_q, peerId, msgId, u"version query failed"_q);
			return;
		}
		const auto version = *latestVersion + 1;
		const auto path = BuildFilePath(item, version);
		if (path.isEmpty()) {
			return;
		}
		const auto dir = QFileInfo(path).absolutePath();
		if (!QDir().mkpath(dir)) {
			AppendEvent(userId, 2, u"error"_q, peerId, msgId, u"mkpath failed: %1"_q.arg(dir));
			return;
		}
		auto fresh = MonitorFile();
		fresh.userId = userId;
		fresh.mediaId = media.mediaId;
		fresh.peerId = peerId;
		fresh.topicId = item->topicRootId().bare;
		fresh.messageId = msgId;
		fresh.version = version;
		fresh.type = typeName;
		fresh.filePath = path.toStdString();
		fresh.fileSize = expectedSize;
		fresh.status = int(MonitorFileStatus::pending);
		fresh.date = item->date();
		fresh.downloadedDate = base::unixtime::now();
		const auto rowId = AyuDatabase::Monitor::addMonitorFile(fresh);
		if (!rowId.has_value()) {
			AppendEvent(userId, 2, u"error"_q, peerId, msgId, u"db insert failed"_q);
			return;
		}
		fresh.fakeId = rowId.value();
		row = std::move(fresh);
	}

	const auto rowId = row->fakeId;
	const auto path = QString::fromStdString(row->filePath);
	const auto finish = [=](bool ok) {
		if (auto existing = AyuDatabase::Monitor::getMonitorFileById(userId, rowId)) {
			existing->status = int(ok ? MonitorFileStatus::done : MonitorFileStatus::failed);
			existing->errorInfo = ok ? "" : "download failed";
			existing->downloadedDate = base::unixtime::now();
			AyuDatabase::Monitor::updateMonitorFile(*existing);
			AppendEvent(
				userId,
				ok ? 0 : 2,
				u"download"_q,
				peerId,
				msgId,
				ok ? u"saved: %1"_q.arg(path) : u"failed: %1"_q.arg(path));
		}
	};

	if (media.document) {
		EnqueueDocumentDownload(session, media.document, origin, path, finish);
	} else {
		EnqueuePhotoDownload(session, media.photo, *photoSize, origin, path, finish);
	}
}

} // namespace

void SubscribeSession(not_null<Main::Session*> session) {
	session->lifetime().add([=] {
		ClearSessionDownloads(session);
	});

	AyuDatabase::Monitor::failPendingMonitorFiles(
		session->userId().bare & PeerId::kChatTypeMask);

	session->data().newItemAdded(
	) | rpl::filter([=](not_null<HistoryItem*> item) {
		return IsMonitored(item);
	}) | rpl::on_next([=](not_null<HistoryItem*> item) {
		EnsureMediaDownloaded(item);
	}, session->lifetime());
}

void HandleEditPreApply(not_null<HistoryItem*> item) {
	EnsureMediaDownloaded(item);
}

void HandleEditPostApply(not_null<HistoryItem*> item) {
	EnsureMediaDownloaded(item);
}

void HandleItemDeleted(not_null<HistoryItem*> item) {
	EnsureMediaDownloaded(item);
}

} // namespace AyuFeatures::Monitor
