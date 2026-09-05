// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "data/data_file_origin.h"
#include "data/data_photo.h"

#include <optional>

class DocumentData;
class HistoryItem;
class PhotoData;

namespace Main {
class Session;
} // namespace Main

namespace AyuFeatures::Monitor {

// PhotoData::validSizeIndex only looks upward from the requested size
// and PhotoMedia::loaded() checks the Large slot only, so a photo without
// a Large size would never finish when loaded as Large. Ask for the
// largest size the photo actually has instead.
[[nodiscard]] std::optional<Data::PhotoSize> ResolveBestPhotoSize(
	not_null<PhotoData*> photo);

void DownloadDocument(
	not_null<Main::Session*> session,
	not_null<DocumentData*> document,
	Data::FileOrigin origin,
	const QString &path,
	Fn<void(bool)> done);

void DownloadPhoto(
	not_null<Main::Session*> session,
	not_null<PhotoData*> photo,
	Data::PhotoSize size,
	Data::FileOrigin origin,
	const QString &path,
	Fn<void(bool)> done);

} // namespace AyuFeatures::Monitor
