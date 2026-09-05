// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include <string>

using ID = long long;

class AyuMessageBase
{
public:
	ID fakeId;
	ID userId;
	ID dialogId;
	ID groupedId;
	ID peerId;
	ID fromId;
	ID topicId;
	int messageId;
	int date;
	int flags;
	int editDate;
	int views;
	int fwdFlags;
	ID fwdFromId;
	std::string fwdName;
	int fwdDate;
	std::string fwdPostAuthor;
	std::string postAuthor;
	int replyFlags;
	int replyMessageId;
	ID replyPeerId;
	int replyTopId;
	bool replyForumTopic;
	std::vector<char> replySerialized;
	std::vector<char> replyMarkupSerialized;
	int entityCreateDate;
	std::string text;
	std::vector<char> textEntities;
	std::string mediaPath;
	std::string hqThumbPath;
	int documentType;
	std::vector<char> documentSerialized;
	std::vector<char> thumbsSerialized;
	std::vector<char> documentAttributesSerialized;
	std::string mimeType;
};

class DeletedMessage : public AyuMessageBase
{
};

class EditedMessage : public AyuMessageBase
{
};

class DeletedDialog
{
public:
	ID fakeId;
	ID userId;
	ID dialogId;
	ID peerId;
	std::unique_ptr<int> folderId; // nullable
	int topMessage;
	int lastMessageDate;
	int flags;
	int entityCreateDate;
};

class RegexFilter
{
public:
	std::vector<char> id;
	std::string text;
	bool enabled;
	bool reversed;
	bool caseInsensitive;
	std::optional<ID> dialogId; // nullable

	bool operator==(const RegexFilter &other) const {
		return id == other.id &&
			text == other.text &&
			caseInsensitive == other.caseInsensitive &&
			reversed == other.reversed &&
			dialogId == other.dialogId &&
			enabled == other.enabled;
	}
	[[nodiscard]] QJsonObject toJson() const {
		QJsonObject json;
		json["id"] = QString::fromUtf8(id.data());
		json["text"] = QString::fromStdString(text);
		json["enabled"] = enabled;
		json["reversed"] = reversed;
		json["caseInsensitive"] = caseInsensitive;
		if (dialogId.has_value()) {
			json["dialogId"] = dialogId.value();
		}
		return json;
	}
};

class RegexFilterGlobalExclusion
{
public:
	ID fakeId;
	ID dialogId;
	std::vector<char> filterId;

	bool operator==(const RegexFilterGlobalExclusion& other) const {
		return dialogId == other.dialogId && filterId == other.filterId;
	}
};

class SpyMessageRead
{
public:
	ID fakeId;
	ID userId;
	ID dialogId;
	int messageId;
	int entityCreateDate;
};

class SpyMessageContentsRead
{
public:
	ID fakeId;
	ID userId;
	ID dialogId;
	int messageId;
	int entityCreateDate;
};

class MonitorTarget
{
public:
	ID fakeId;
	ID userId;
	ID peerId;
	ID topicId;
	bool enabled;
	std::string mediaTypes;
	int addedDate;
};

enum class MonitorFileStatus : int {
	pending = 0,
	done = 1,
	failed = 2,
	skipped = 3,
};

class MonitorFile
{
public:
	ID fakeId;
	ID userId;
	ID mediaId;
	ID peerId;
	ID topicId;
	int messageId;
	int version;
	std::string type;
	std::string filePath;
	int64 fileSize;
	int status;
	std::string errorInfo;
	int date;
	int downloadedDate;
};

class MonitorEvent
{
public:
	ID fakeId;
	ID userId;
	int date;
	int level;
	std::string category;
	ID peerId;
	int messageId;
	std::string text;
};

struct MonitorFileFilter
{
	ID peerId = 0;       // 0 = any target
	std::string type;    // empty = any type
	int status = -1;     // -1 = any status
	int minVersion = 0;  // 0 = any version
};

struct MonitorFilePage
{
	std::vector<MonitorFile> rows;
	bool endReached = false;
};

struct MonitorTargetStats
{
	ID peerId = 0;
	int doneCount = 0;
	int64 doneBytes = 0;
	int failedCount = 0;
};

struct MonitorGlobalStats
{
	int todayCount = 0;
	int64 todayBytes = 0;
	int totalCount = 0;
	int64 totalBytes = 0;
	int failedCount = 0;
};
