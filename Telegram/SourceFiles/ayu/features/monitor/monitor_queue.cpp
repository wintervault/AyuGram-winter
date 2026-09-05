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
#include <array>
#include <deque>
#include <limits>
#include <set>

#include <QTimer>

namespace AyuFeatures::Monitor {
namespace {

// Keep monitor downloads from crowding out manual ones.
constexpr auto kMaxConcurrent = 3;

constexpr auto kMaxRetries = 3;
constexpr auto kRetryDelays = std::array<crl::time, kMaxRetries>{
	30 * crl::time(1000),
	2 * crl::time(60 * 1000),
	10 * crl::time(60 * 1000),
};

struct Task {
	not_null<Main::Session*> session;
	DocumentData *document = nullptr;
	PhotoData *photo = nullptr;
	Data::PhotoSize photoSize = Data::PhotoSize::Large;
	Data::FileOrigin origin;
	QString path;
	Fn<void(bool)> done;

	// Retry bookkeeping: tasks wait in the queue until notBefore.
	int retries = 0;
	crl::time notBefore = 0;
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

rpl::event_stream<> &ChangedStream() {
	static rpl::event_stream<> result;
	return result;
}

std::multiset<QString> &ActivePaths() {
	static std::multiset<QString> result;
	return result;
}

// Leaked singleton: no destruction-order issues at app exit.
QTimer &TickTimer() {
	static const auto result = new QTimer();
	result->setSingleShot(true);
	return *result;
}

void Pump();
void PumpNow();

void Dispatch(Task task) {
	++ActiveCount();
	ActivePaths().insert(task.path);
	ChangedStream().fire({});
	// Cleared when the session dies. A dead session's failed task must
	// never be re-queued: its pointers dangle after the teardown, and
	// the re-queue happens after ClearSessionDownloads already ran.
	const auto alive = std::make_shared<bool>(true);
	const auto finish = [task, alive](bool ok) mutable {
		--ActiveCount();
		{
			auto &paths = ActivePaths();
			const auto it = paths.find(task.path);
			if (it != paths.end()) {
				paths.erase(it);
			}
		}
		ChangedStream().fire({});
		if (!ok && *alive && task.retries < kMaxRetries) {
			// The row is marked failed by done(ok); retry later keeps
			// it rescueable (in-memory messages refresh file_reference
			// automatically, deleted ones fail for good).
			task.notBefore = crl::now() + kRetryDelays[task.retries];
			++task.retries;
			Queue().push_back(std::move(task));
		} else {
			task.done(ok);
		}
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
	// Registered after the download added its own guard, so on session
	// death the flag clears before any deferred finish closure runs.
	task.session->lifetime().add([alive] {
		*alive = false;
	});
}

// Coalesced entry point: dispatches must happen from a fresh event loop
// iteration, so a dying session always finishes teardown first (its
// ClearSessionDownloads runs on the same destruction stack as the
// finish callbacks that call this) and never gets new dispatches.
void Pump() {
	crl::on_main([] {
		PumpNow();
	});
}

void PumpNow() {
	auto &queue = Queue();
	auto &active = ActiveCount();
	const auto paused = AyuSettings::getInstance().monitorPaused();
	const auto now = crl::now();
	const auto due = [&](const Task &task) {
		return task.notBefore <= now;
	};
	while (!paused && active < kMaxConcurrent && !queue.empty()) {
		const auto it = std::find_if(queue.begin(), queue.end(), due);
		if (it == queue.end()) {
			auto earliest = queue.front().notBefore;
			for (const auto &task : queue) {
				earliest = std::min(earliest, task.notBefore);
			}
			TickTimer().start(int(std::min<crl::time>(
				earliest - now,
				std::numeric_limits<int>::max())));
			break;
		}
		auto task = std::move(*it);
		queue.erase(it);
		Dispatch(std::move(task));
	}
}

void EnsureInitialized() {
	static bool initialized = false;
	if (initialized) {
		return;
	}
	initialized = true;
	QObject::connect(&TickTimer(), &QTimer::timeout, [] {
		Pump();
	});
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
	ChangedStream().fire({});
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
	ChangedStream().fire({});
	Pump();
}

// Also drops not-yet-dispatched retries, they live in the same queue.
void ClearSessionDownloads(not_null<Main::Session*> session) {
	auto &queue = Queue();
	queue.erase(
		std::remove_if(queue.begin(), queue.end(), [&](const Task &task) {
			return task.session == session;
		}),
		queue.end());
	ChangedStream().fire({});
}

QueueSnapshot SnapshotQueue() {
	auto result = QueueSnapshot();
	result.active = ActiveCount();
	result.queued = int(Queue().size());
	for (const auto &path : ActivePaths()) {
		result.activePaths.push_back(path);
	}
	return result;
}

rpl::producer<> QueueChanged() {
	return ChangedStream().events();
}

} // namespace AyuFeatures::Monitor
