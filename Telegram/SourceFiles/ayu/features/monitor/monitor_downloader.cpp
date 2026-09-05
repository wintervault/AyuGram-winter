// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/monitor/monitor_downloader.h"

#include "ayu/data/ayu_database.h"
#include "ayu/data/entities.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/file_download.h"

#include <QFile>
#include <QTimer>

namespace AyuFeatures::Monitor {
namespace {

constexpr auto kDownloadTimeoutMs = 10 * 60 * 1000;

struct DownloadState {
	rpl::lifetime lifetime;
	bool finished = false;
};

void Finish(
		const std::shared_ptr<DownloadState> &state,
		Fn<void(bool)> done,
		bool ok) {
	if (state->finished) {
		return;
	}
	state->finished = true;
	const auto copy = done;
	crl::on_main([state, copy, ok] {
		copy(ok);
	});
}

} // namespace

std::optional<Data::PhotoSize> ResolveBestPhotoSize(
		not_null<PhotoData*> photo) {
	for (const auto size : {
		Data::PhotoSize::Large,
		Data::PhotoSize::Thumbnail,
		Data::PhotoSize::Small,
	}) {
		if (photo->hasExact(size)) {
			return size;
		}
	}
	return std::nullopt;
}

void DownloadDocument(
		not_null<Main::Session*> session,
		not_null<DocumentData*> document,
		Data::FileOrigin origin,
		const QString &path,
		Fn<void(bool)> done) {
	const auto state = std::make_shared<DownloadState>();
	// Release the queue slot and defang all pending callbacks when the
	// session dies: the state owns every subscription, and the timeout
	// timer checks the flag before touching the document.
	session->lifetime().add([state, done] {
		Finish(state, done, false);
	});
	QTimer::singleShot(kDownloadTimeoutMs, [=] {
		if (state->finished) {
			return;
		}
		document->cancel();
		Finish(state, done, false);
	});

	crl::on_main([=, done = std::move(done)]() mutable {
		document->save(origin, path);

		if (!document->loading() && QFile::exists(path)) {
			Finish(state, done, QFile(path).size() == document->size);
			return;
		}

		const auto documentId = document->id;
		session->data().documentLoadProgress(
		) | rpl::filter([=](not_null<DocumentData*> changed) {
			return changed->id == documentId && !changed->loading();
		}) | rpl::take(1) | rpl::on_next([=](not_null<DocumentData*> changed) mutable {
			const auto file = QFile(path);
			Finish(state, done, file.exists() && file.size() == changed->size);
		}, state->lifetime);
	});
}

void DownloadPhoto(
		not_null<Main::Session*> session,
		not_null<PhotoData*> photo,
		Data::PhotoSize size,
		Data::FileOrigin origin,
		const QString &path,
		Fn<void(bool)> done) {
	const auto state = std::make_shared<DownloadState>();
	session->lifetime().add([state, done] {
		Finish(state, done, false);
	});
	QTimer::singleShot(kDownloadTimeoutMs, [state, done] {
		Finish(state, done, false);
	});

	crl::on_main([=, done = std::move(done)]() mutable {
		const auto view = photo->createMediaView();
		if (!view) {
			Finish(state, done, false);
			return;
		}
		view->wanted(size, origin);

		// Mirrors PhotoMedia::saveToFile, but for the resolved size:
		// saveToFile always reads the Large slot, which stays empty for
		// photos without a Large size.
		auto trySave = [=]() mutable {
			if (!view->image(size)) {
				return false;
			}
			auto ok = false;
			if (const auto video = view->videoContent(size); !video.isEmpty()) {
				QFile f(path);
				ok = f.open(QIODevice::WriteOnly)
					&& (f.write(video) == video.size());
			} else if (const auto bytes = view->imageBytes(size); !bytes.isEmpty()) {
				QFile f(path);
				ok = f.open(QIODevice::WriteOnly)
					&& (f.write(bytes) == bytes.size());
			} else {
				ok = view->image(size)->original().save(path, "JPG");
			}
			Finish(state, done, ok);
			return true;
		};

		if (trySave()) {
			return;
		}

		session->downloaderTaskFinished(
		) | rpl::filter([=] {
			return view->image(size) != nullptr;
		}) | rpl::take(1) | rpl::on_next([=]() mutable {
			trySave();
		}, state->lifetime);
	});
}

} // namespace AyuFeatures::Monitor
