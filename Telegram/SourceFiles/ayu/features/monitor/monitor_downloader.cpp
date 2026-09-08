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

#include <map>

namespace AyuFeatures::Monitor {
namespace {

// Photo files are small and their failures resolve fast, the plain
// total-time cap is enough for them.
constexpr auto kPhotoTimeoutMs = 10 * 60 * 1000;
// A document dies when no data chunk arrived for this long. Covers the
// server-told FLOOD_WAIT waits (usually <= 3 min) while turning a
// genuinely stuck download into a 4-minute slot release.
constexpr auto kStallTimeoutMs = 4 * 60 * 1000;
// Absolute lifetime of one document attempt even if data keeps flowing:
// 60 min @ ~290 KB/s spans the default 1 GB size cap.
constexpr auto kMaxDownloadMs = 60 * 60 * 1000;

struct DownloadState {
	rpl::lifetime lifetime;
	bool finished = false;
};

void Finish(
		const std::shared_ptr<DownloadState> &state,
		Fn<void(bool, DownloadFailure)> done,
		bool ok,
		DownloadFailure reason) {
	if (state->finished) {
		return;
	}
	state->finished = true;
	// Break the state -> lifetime -> subscription -> state ownership
	// cycle; also stops stale filters and the stall watchdog from
	// running after the finish.
	state->lifetime.destroy();
	const auto copy = done;
	crl::on_main([state, copy, ok, reason] {
		copy(ok, reason);
	});
}

} // namespace

std::map<Main::Session*, rpl::lifetime> &SessionLifetimes() {
	static std::map<Main::Session*, rpl::lifetime> result;
	return result;
}

rpl::lifetime &MonitorSessionLifetime(not_null<Main::Session*> session) {
	auto &lifetimes = SessionLifetimes();
	return lifetimes.try_emplace(session.get()).first->second;
}

void ClearMonitorSessionLifetime(not_null<Main::Session*> session) {
	auto &lifetimes = SessionLifetimes();
	const auto it = lifetimes.find(session.get());
	if (it != lifetimes.end()) {
		it->second.destroy();
		lifetimes.erase(it);
	}
}

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
		Fn<void(bool, DownloadFailure)> done) {
	const auto state = std::make_shared<DownloadState>();
	// Releases the queue slot and defangs all pending callbacks when
	// the session dies: the state owns every subscription, and both
	// timers and the deferred start check the flag before touching
	// the document.
	MonitorSessionLifetime(session).add([state, done] {
		Finish(state, done, false, DownloadFailure::SessionEnd);
	});
	QTimer::singleShot(kMaxDownloadMs, [state, document, done] {
		if (state->finished) {
			return;
		}
		// Finish first: it drops the progress subscription, so the
		// synchronous done event fired by cancel() below cannot reenter
		// the final-judge branch and steal the reason (cancel() also
		// removes the partial file, which would read as NotFound).
		Finish(state, done, false, DownloadFailure::Timeout);
		document->cancel();
	});

	// Stall watchdog: a slow-but-alive download keeps restarting it
	// from progress events, a stuck one runs out and frees the slot.
	// The functor captures a raw pointer on purpose: a shared_ptr here
	// would be held by the sender's own connection and never freed.
	// Deleting a QObject inside its own timeout slot is safe on the
	// Qt in use (ConnectionData refcounting + senderDeleted check,
	// qobject.cpp activate), and the timer dies via lifetime.destroy()
	// inside Finish.
	const auto stall = std::make_shared<QTimer>();
	const auto stallRaw = stall.get();
	stall->setSingleShot(true);
	QObject::connect(stallRaw, &QTimer::timeout, [state, document, done] {
		if (state->finished) {
			return;
		}
		// Finish before cancel: same reentry guard as the cap timer.
		Finish(state, done, false, DownloadFailure::Stall);
		document->cancel();
	});
	state->lifetime.add([stall] {
		stall->stop();
	});
	stall->start(kStallTimeoutMs);

	crl::on_main([=, done = std::move(done)]() mutable {
		if (state->finished) {
			return;
		}
		if (document->loading()) {
			// DocumentData has a single shared loader, which could be
			// busy with a manual save; canceling it would kill that
			// download. Fail fast instead, the queue retries later.
			Finish(state, done, false, DownloadFailure::LoadingConflict);
			return;
		}
		document->save(origin, path);

		// No loader after save() means the data was available locally:
		// either the file was written or writing it failed right away.
		if (!document->loading()) {
			const auto file = QFile(path);
			const auto ok = file.exists() && file.size() == document->size;
			Finish(state, done, ok, ok ? DownloadFailure::None : DownloadFailure::Io);
			return;
		}

		const auto documentId = document->id;
		session->data().documentLoadProgress(
		) | rpl::filter([=](not_null<DocumentData*> changed) {
			return changed->id == documentId;
		}) | rpl::on_next([=](not_null<DocumentData*> changed) mutable {
			if (state->finished) {
				return;
			}
			if (changed->loading()) {
				// Alive: every data chunk resets the stall watchdog.
				stall->start(kStallTimeoutMs);
				return;
			}
			// Done, failed or cancelled: judge by the file on disk.
			stall->stop();
			const auto file = QFile(path);
			const auto ok = file.exists() && file.size() == changed->size;
			Finish(state, done, ok, ok
				? DownloadFailure::None
				: (file.exists()
					? DownloadFailure::SizeMismatch
					: DownloadFailure::NotFound));
		}, state->lifetime);
	});
}

void DownloadPhoto(
		not_null<Main::Session*> session,
		not_null<PhotoData*> photo,
		Data::PhotoSize size,
		Data::FileOrigin origin,
		const QString &path,
		Fn<void(bool, DownloadFailure)> done) {
	const auto state = std::make_shared<DownloadState>();
	MonitorSessionLifetime(session).add([state, done] {
		Finish(state, done, false, DownloadFailure::SessionEnd);
	});
	QTimer::singleShot(kPhotoTimeoutMs, [state, done] {
		Finish(state, done, false, DownloadFailure::Timeout);
	});

	crl::on_main([=, done = std::move(done)]() mutable {
		if (state->finished) {
			return;
		}
		const auto view = photo->createMediaView();
		if (!view) {
			Finish(state, done, false, DownloadFailure::NotFound);
			return;
		}
		// A previous failed attempt leaves CloudFile::Flag::Failed set,
		// wanted() would silently no-op until it is cleared (upstream
		// clears it the same way before a manual retry).
		photo->clearFailed(size);
		view->wanted(size, origin);

		// Mirrors PhotoMedia::saveToFile, but for the resolved size and
		// without the video branch: video bytes of video-photos only
		// exist if the user happened to preview them, while the file
		// name and the recorded size always describe the image frame.
		auto trySave = [=]() mutable {
			if (!view->image(size)) {
				return false;
			}
			auto ok = false;
			if (const auto bytes = view->imageBytes(size); !bytes.isEmpty()) {
				QFile f(path);
				ok = f.open(QIODevice::WriteOnly)
					&& (f.write(bytes) == bytes.size());
			} else {
				ok = view->image(size)->original().save(path, "JPG");
			}
			Finish(state, done, ok, ok ? DownloadFailure::None : DownloadFailure::Io);
			return true;
		};

		if (trySave()) {
			return;
		}

		// Progress events fire for loads and for failures; a failed
		// load never fills the image, so it fails the attempt instead
		// of burning the timeout.
		session->data().photoLoadProgress(
		) | rpl::filter([=](not_null<PhotoData*> changed) {
			return changed == photo
				&& (view->image(size) != nullptr || photo->failed(size));
		}) | rpl::take(1) | rpl::on_next([=]() mutable {
			if (view->image(size)) {
				trySave();
			} else {
				Finish(state, done, false, DownloadFailure::FailedLoad);
			}
		}, state->lifetime);
	});
}

} // namespace AyuFeatures::Monitor
