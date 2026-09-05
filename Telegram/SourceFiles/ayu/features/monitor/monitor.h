// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

class HistoryItem;

namespace Main {
class Session;
} // namespace Main

namespace AyuFeatures::Monitor {

[[nodiscard]] QString ResolveSaveRoot();
[[nodiscard]] QString DefaultNameTemplate();

void SubscribeSession(not_null<Main::Session*> session);
void HandleEditPreApply(not_null<HistoryItem*> item);
void HandleEditPostApply(not_null<HistoryItem*> item);
void HandleItemDeleted(not_null<HistoryItem*> item);

} // namespace AyuFeatures::Monitor
