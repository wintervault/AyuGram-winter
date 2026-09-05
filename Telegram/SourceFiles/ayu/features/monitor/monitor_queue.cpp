// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/monitor/monitor_queue.h"

#include "ayu/ayu_settings.h"
#include "ayu/features/monitor/monitor_downloader.h"
#include "main/main_session.h"

#include <algorithm>
#include <deque>

namespace AyuFeatures::Monitor {
namespace {

// Keep monitor downloads from crowding out manual ones.
constexpr auto kMaxConcurrent = 3;

struct Task {
	not_null<Main::Session*> session;
	DocumentData *document = nullptr;
	PhotoData *photo = nullptr;
	Data::PhotoSize photoSize = Data::PhotoSize::Large;
	Data::FileOrigin origin;
	QString path;
	Fn<void(bool)> done;
};

std::deque<Task> &Queue() {
	static std::deque<Task> result;
	return result;
}

int &ActiveCount() {
	static int result = 0;
	return result;
}

rpl::lifetime &QueueLifetime() {
	static rpl::lifetime result;
	return result;
}

void Pump() {
	auto &queue = Queue();
	auto &active = ActiveCount();
	const auto paused = AyuSettings::getInstance().monitorPaused();
	while (!paused && active < kMaxConcurrent && !queue.empty()) {
		auto task = std::move(queue.front());
		queue.pop_front();
		++active;
		const auto finish = [done = std::move(task.done)](bool ok) mutable {
			--ActiveCount();
			done(ok);
			Pump();
		};
		if (task.document) {
			DownloadDocument(
				task.session,
				task.document,
				task.origin,
				task.path,
				finish);
		} else {
			DownloadPhoto(
				task.session,
				task.photo,
				task.photoSize,
				task.origin,
				task.path,
				finish);
		}
	}
}

void EnsureInitialized() {
	static bool initialized = false;
	if (initialized) {
		return;
	}
	initialized = true;
	AyuSettings::getInstance().monitorPausedValue()
	| rpl::filter([=](bool paused) {
		return !paused;
	}) | rpl::on_next([=] {
		Pump();
	}, QueueLifetime());
}

} // namespace

void EnqueueDocumentDownload(
		not_null<Main::Session*> session,
		not_null<DocumentData*> document,
		Data::FileOrigin origin,
		const QString &path,
		Fn<void(bool)> done) {
	Queue().push_back({
		session,
		document,
		nullptr,
		Data::PhotoSize::Large,
		origin,
		path,
		std::move(done),
	});
	EnsureInitialized();
	Pump();
}

void EnqueuePhotoDownload(
		not_null<Main::Session*> session,
		not_null<PhotoData*> photo,
		Data::PhotoSize size,
		Data::FileOrigin origin,
		const QString &path,
		Fn<void(bool)> done) {
	Queue().push_back({
		session,
		nullptr,
		photo,
		size,
		origin,
		path,
		std::move(done),
	});
	EnsureInitialized();
	Pump();
}

void ClearSessionDownloads(not_null<Main::Session*> session) {
	auto &queue = Queue();
	queue.erase(
		std::remove_if(queue.begin(), queue.end(), [&](const Task &task) {
			return task.session == session;
		}),
		queue.end());
}

} // namespace AyuFeatures::Monitor
