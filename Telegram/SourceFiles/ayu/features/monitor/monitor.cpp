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

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStorageInfo>

#include <map>
#include <tuple>

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

// Windows device names match the stem before the first dot, and the
// file system ignores trailing dots and spaces in every name part.
[[nodiscard]] QString SanitizeSegment(QString segment) {
	segment = SanitizeNamePart(segment);
	static const auto reserved = QSet<QString>{
		u"CON"_q, u"PRN"_q, u"AUX"_q, u"NUL"_q,
		u"COM1"_q, u"COM2"_q, u"COM3"_q, u"COM4"_q, u"COM5"_q,
		u"COM6"_q, u"COM7"_q, u"COM8"_q, u"COM9"_q,
		u"LPT1"_q, u"LPT2"_q, u"LPT3"_q, u"LPT4"_q, u"LPT5"_q,
		u"LPT6"_q, u"LPT7"_q, u"LPT8"_q, u"LPT9"_q,
	};
	if (reserved.contains(segment.section('.', 0, 0).toUpper())) {
		segment = '_' + segment;
	}
	while (segment.endsWith('.') || segment.endsWith(' ')) {
		segment.chop(1);
	}
	return segment;
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

struct TargetsCache {
	ID userId = 0;
	std::vector<MonitorTarget> targets;
	crl::time fetched = 0;
};

TargetsCache &TargetsCacheInstance() {
	static TargetsCache result;
	return result;
}

[[nodiscard]] std::vector<MonitorTarget> GetTargetsCached(ID userId) {
	constexpr auto kTtl = 5 * crl::time(1000);
	auto &cache = TargetsCacheInstance();
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

// Per-target mediaTypes field: a comma-separated whitelist of type
// names. An empty value means the global type toggles apply as-is.
[[nodiscard]] bool TargetAllowsType(
		ID userId,
		ID peerId,
		ID topicId,
		MediaType type) {
	const auto targets = GetTargetsCached(userId);
	const auto name = TypeName(type);
	for (const auto &target : targets) {
		if (!target.enabled || target.peerId != peerId) {
			continue;
		}
		if (target.topicId != 0 && target.topicId != topicId) {
			continue;
		}
		if (target.mediaTypes.empty()) {
			return true;
		}
		const auto parts = QString::fromStdString(target.mediaTypes)
			.split(',', Qt::SkipEmptyParts);
		for (const auto &part : parts) {
			if (part.trimmed() == name) {
				return true;
			}
		}
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

// Config-driven skips can fire for every message in a high-traffic
// channel and would flush the shared event log ring; throttle them
// per target and reason.
void AppendSkipEvent(
		ID userId,
		int level,
		ID peerId,
		int messageId,
		const QString &reason,
		const QString &text) {
	constexpr auto kThrottle = 60 * crl::time(1000);
	using Key = std::tuple<ID, ID, QString>;
	static std::map<Key, crl::time> last;
	const auto now = crl::now();
	const auto key = Key{ userId, peerId, reason };
	const auto it = last.find(key);
	if (it != last.end() && now - it->second < kThrottle) {
		return;
	}
	last[key] = now;
	AppendEvent(userId, level, u"skip"_q, peerId, messageId, text);
}

[[nodiscard]] std::optional<PendingMedia> ExtractMedia(
		not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return std::nullopt;
	}
	if (media->webpage()) {
		// A link preview is not message media: its photo/document
		// belongs to the linked page, don't archive it.
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
		{ u"ext"_q, SanitizeNamePart(origInfo.suffix()) },
		{ u"orig_name"_q, SanitizeNamePart(origName) },
		{ u"date"_q, date },
		{ u"yyyy-MM-dd"_q, date },
	};
	for (const auto &[key, value] : vars) {
		relative.replace('{' + key + '}', value);
	}

	// Split into segments, dropping traversal, reserved names and
	// anything that ends up empty after sanitizing.
	auto segments = QStringList();
	for (const auto &part : relative.split('/', Qt::SkipEmptyParts)) {
		const auto segment = SanitizeSegment(part);
		if (!segment.isEmpty()) {
			segments.push_back(segment);
		}
	}
	if (segments.isEmpty()) {
		return QString();
	}

	// Version suffix goes before the final file name extension, and the
	// base name is shortened when the full path gets near the legacy
	// Windows MAX_PATH limit.
	const auto info = QFileInfo(segments.back());
	const auto ext = info.suffix().isEmpty() ? QString() : u'.' + info.suffix();
	auto base = info.completeBaseName();
	if (base.isEmpty()) {
		base = info.fileName();
	}
	const auto versionTag = version > 1
		? u"_v%1"_q.arg(version)
		: QString();
	auto dirPart = ResolveSaveRoot();
	if (segments.size() > 1) {
		dirPart += segments.mid(0, segments.size() - 1).join('/') + '/';
	}

	constexpr auto kMaxPathLen = 240;
	auto keep = kMaxPathLen
		- dirPart.size()
		- ext.size()
		- versionTag.size()
		- 5; // room for the short hash appended after truncation
	if (keep < 1) {
		// The directory part alone busts the budget; still keep a sane
		// file name, the download fails later if the path is too long.
		keep = 1;
	}
	if (base.size() > keep) {
		base = base.left(keep);
		if (base.at(base.size() - 1).isHighSurrogate()) {
			// Don't split a surrogate pair.
			base.chop(1);
		}
		const auto digest = QCryptographicHash::hash(
			base.toUtf8(),
			QCryptographicHash::Md5).toHex().left(4);
		base += u"_%1"_q.arg(QString::fromLatin1(digest));
	}
	segments.back() = base + versionTag + ext;
	return dirPart + segments.back();
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
		// Global toggles are visible in the settings, no event spam.
		return;
	}
	if (!TargetAllowsType(userId, peerId, item->topicRootId().bare, media.type)) {
		AppendSkipEvent(
			userId,
			0,
			peerId,
			msgId,
			u"target-type"_q,
			u"type not allowed by target: %1"_q.arg(TypeName(media.type)));
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
		AppendSkipEvent(
			userId,
			1,
			peerId,
			msgId,
			u"oversize"_q,
			u"oversize: %1"_q.arg(expectedSize));
		return;
	}

	const auto minFreeBytes = int64(settings.monitorMinDiskSpaceMB()) * 1024 * 1024;
	if (minFreeBytes > 0) {
		const auto root = ResolveSaveRoot();
		if (!QFileInfo::exists(root)) {
			QDir().mkpath(root);
		}
		const auto storage = QStorageInfo(root);
		const auto available = storage.bytesAvailable();
		if (storage.isValid() && available >= 0 && available < minFreeBytes) {
			AppendSkipEvent(
				userId,
				1,
				peerId,
				msgId,
				u"low-disk"_q,
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
		// The file (or its whole directory) may have been removed while
		// the record still says done: recreate the directory or the
		// retry would fail for good.
		QDir().mkpath(QFileInfo(
			QString::fromStdString(existing->filePath)).absolutePath());
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
		auto path = BuildFilePath(item, version);
		if (path.isEmpty()) {
			return;
		}
		// A template without a unique variable can map different
		// messages to the same path; never overwrite an existing
		// file, suffix it instead.
		if (QFileInfo::exists(path)) {
			const auto info = QFileInfo(path);
			const auto base = info.completeBaseName();
			const auto ext = info.suffix().isEmpty()
				? QString()
				: u'.' + info.suffix();
			auto seq = 1;
			while (true) {
				const auto candidate = info.dir().filePath(
					base + u"_%1"_q.arg(seq) + ext);
				if (!QFileInfo::exists(candidate)) {
					path = candidate;
					break;
				}
				++seq;
			}
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
			if (!ok
				&& existing->status == int(MonitorFileStatus::done)
				&& QFileInfo::exists(path)) {
				// A sibling task for this row already completed, don't
				// downgrade the row over its finished file.
				return;
			}
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

void InvalidateTargetsCache() {
	auto &cache = TargetsCacheInstance();
	cache.userId = 0;
	cache.targets.clear();
	cache.fetched = 0;
}

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
