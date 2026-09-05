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

void DownloadDocument(
		not_null<Main::Session*> session,
		not_null<DocumentData*> document,
		Data::FileOrigin origin,
		const QString &path,
		Fn<void(bool)> done) {
	const auto state = std::make_shared<DownloadState>();
	QTimer::singleShot(kDownloadTimeoutMs, [state, done] {
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
		Data::FileOrigin origin,
		const QString &path,
		Fn<void(bool)> done) {
	const auto state = std::make_shared<DownloadState>();
	QTimer::singleShot(kDownloadTimeoutMs, [state, done] {
		Finish(state, done, false);
	});

	crl::on_main([=, done = std::move(done)]() mutable {
		const auto view = photo->createMediaView();
		if (!view) {
			Finish(state, done, false);
			return;
		}
		view->wanted(Data::PhotoSize::Large, origin);

		auto trySave = [=]() mutable {
			if (!view->loaded()) {
				return false;
			}
			Finish(state, done, view->saveToFile(path));
			return true;
		};

		if (trySave()) {
			return;
		}

		session->downloaderTaskFinished(
		) | rpl::filter([=] {
			return view->loaded();
		}) | rpl::take(1) | rpl::on_next([=]() mutable {
			trySave();
		}, state->lifetime);
	});
}

} // namespace AyuFeatures::Monitor
