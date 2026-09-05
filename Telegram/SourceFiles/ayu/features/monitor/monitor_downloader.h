// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "data/data_file_origin.h"

class DocumentData;
class HistoryItem;
class PhotoData;

namespace Main {
class Session;
} // namespace Main

namespace AyuFeatures::Monitor {

void DownloadDocument(
	not_null<Main::Session*> session,
	not_null<DocumentData*> document,
	Data::FileOrigin origin,
	const QString &path,
	Fn<void(bool)> done);

void DownloadPhoto(
	not_null<Main::Session*> session,
	not_null<PhotoData*> photo,
	Data::FileOrigin origin,
	const QString &path,
	Fn<void(bool)> done);

} // namespace AyuFeatures::Monitor
