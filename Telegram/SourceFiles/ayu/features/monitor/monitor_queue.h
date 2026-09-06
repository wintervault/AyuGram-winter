// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "data/data_file_origin.h"
#include "data/data_photo.h"
#include "rpl/producer.h"

#include <vector>

class DocumentData;
class PhotoData;

namespace Main {
class Session;
} // namespace Main

namespace AyuFeatures::Monitor {

// Keep monitor downloads from crowding out manual ones.
inline constexpr auto kMaxConcurrent = 3;

void EnqueueDocumentDownload(
	not_null<Main::Session*> session,
	not_null<DocumentData*> document,
	Data::FileOrigin origin,
	const QString &path,
	Fn<void(bool)> done);

void EnqueuePhotoDownload(
	not_null<Main::Session*> session,
	not_null<PhotoData*> photo,
	Data::PhotoSize size,
	Data::FileOrigin origin,
	const QString &path,
	Fn<void(bool)> done);

// Drop queued (not yet started) downloads of a session, on its end.
void ClearSessionDownloads(not_null<Main::Session*> session);

struct QueueSnapshot {
	int active = 0;
	int queued = 0;
	std::vector<QString> activePaths;
};

[[nodiscard]] QueueSnapshot SnapshotQueue();
[[nodiscard]] rpl::producer<> QueueChanged();

} // namespace AyuFeatures::Monitor
