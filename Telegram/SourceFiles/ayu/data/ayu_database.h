// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ayu/data/entities.h"

#include <functional>

class SchemaVersion
{
public:
	int id;
	int version;
};

namespace AyuDatabase {

void initialize();

void addEditedMessage(const EditedMessage &message);
std::vector<EditedMessage> getEditedMessages(ID userId, ID dialogId, ID messageId, ID minId, ID maxId, int totalLimit);
bool hasRevisions(ID userId, ID dialogId, ID messageId);

void addDeletedMessage(const DeletedMessage &message);
std::vector<DeletedMessage> getDeletedMessages(ID userId, ID dialogId, ID topicId, ID minId, ID maxId, int totalLimit, const std::string &searchQuery = "");
bool hasDeletedMessages(ID userId, ID dialogId, ID topicId);
void removeDeletedMessage(ID userId, ID dialogId, ID messageId);
void clearDeletedMessages(ID userId, ID dialogId, ID topicId);

std::vector<RegexFilter> getAllRegexFilters();
RegexFilter getById(std::vector<char> id);
std::vector<RegexFilter> getShared();
std::vector<RegexFilter> getByDialogId(ID dialogId);
std::vector<RegexFilterGlobalExclusion> getAllFiltersExclusions();
std::vector<RegexFilter> getExcludedByDialogId(ID dialogId);

int getCount();


void addRegexFilter(const RegexFilter &filter);
void addRegexExclusion(const RegexFilterGlobalExclusion &exclusion);

void updateRegexFilter(const RegexFilter &filter);

void deleteFilter(const std::vector<char> &id);
void deleteExclusionsByFilterId(const std::vector<char> &id);
void deleteExclusion(ID dialogId, std::vector<char> filterId);

void deleteAllFilters();
void deleteAllExclusions();

bool hasFilters();
bool hasPerDialogFilters();

void moveCurrentDatabase();

namespace Monitor {

std::vector<MonitorTarget> getAllMonitorTargets(ID userId);
std::optional<MonitorTarget> getMonitorTarget(ID userId, ID peerId, ID topicId);
void upsertMonitorTarget(const MonitorTarget &target);
void removeMonitorTarget(ID userId, ID peerId, ID topicId);

bool hasMonitorFile(ID userId, ID mediaId);
std::optional<MonitorFile> getMonitorFile(ID userId, ID mediaId);
std::optional<MonitorFile> getMonitorFileById(ID userId, ID rowId);
std::optional<ID> addMonitorFile(const MonitorFile &file);
void updateMonitorFile(const MonitorFile &file);
std::optional<int> getLatestFileVersion(ID userId, ID peerId, int messageId);
std::vector<MonitorFile> getMonitorFiles(ID userId, int limit);

void addMonitorEvent(const MonitorEvent &event);
std::vector<MonitorEvent> getMonitorEvents(ID userId, int limit);

}

}
