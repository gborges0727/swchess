#include "save/native.h"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace swchess::save {
namespace {

using json = nlohmann::json;

// Reads one key of the expected type. A missing or wrongly typed key keeps the
// value the caller already holds, so an older document still loads.
template <class T>
void readField(const json& doc, const char* key, T& out) {
    auto it = doc.find(key);
    if (it == doc.end()) return;
    if (!it->is_null()) {
        try {
            out = it->get<T>();
        } catch (const json::exception&) {
        }
    }
}

json settingsToJson(const NativeSettings& s) {
    return json{{"set", s.set},           {"language", s.language},
                {"cadence", s.cadence},   {"walking", s.walking},
                {"captures", s.captures}, {"sounds", s.sounds}};
}

void settingsFromJson(const json& doc, NativeSettings& s) {
    readField(doc, "set", s.set);
    readField(doc, "language", s.language);
    readField(doc, "cadence", s.cadence);
    readField(doc, "walking", s.walking);
    readField(doc, "captures", s.captures);
    readField(doc, "sounds", s.sounds);
}

}  // namespace

std::string writeNative(const NativeSave& save) {
    json doc;
    doc["version"] = save.version;
    doc["start_fen"] = save.startFen;
    doc["moves"] = save.moves;
    doc["current_ply"] = save.currentPly;
    doc["white"] = json{{"name", save.whiteName}, {"type", playerTypeName(save.whiteType)}};
    doc["black"] = json{{"name", save.blackName}, {"type", playerTypeName(save.blackType)}};
    doc["settings"] = settingsToJson(save.settings);
    return doc.dump(2) + "\n";
}

Result<NativeSave> readNative(std::string_view text) {
    json doc = json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        return {std::nullopt, "the save is not a JSON object"};
    }

    NativeSave save;
    readField(doc, "version", save.version);
    if (save.version > kNativeVersion) {
        return {std::nullopt, "the save is version " + std::to_string(save.version) +
                                  ", which this build does not read"};
    }
    readField(doc, "start_fen", save.startFen);
    readField(doc, "moves", save.moves);
    readField(doc, "current_ply", save.currentPly);

    auto readPlayer = [&](const char* key, std::string& name, PlayerType& type) -> std::string {
        auto it = doc.find(key);
        if (it == doc.end() || !it->is_object()) return {};
        readField(*it, "name", name);
        std::string typeName = playerTypeName(type);
        readField(*it, "type", typeName);
        auto parsed = playerTypeFromName(typeName);
        if (!parsed) return std::string(key) + " is neither human nor computer";
        type = *parsed;
        return {};
    };
    if (std::string err = readPlayer("white", save.whiteName, save.whiteType); !err.empty()) {
        return {std::nullopt, err};
    }
    if (std::string err = readPlayer("black", save.blackName, save.blackType); !err.empty()) {
        return {std::nullopt, err};
    }

    auto settings = doc.find("settings");
    if (settings != doc.end() && settings->is_object()) {
        settingsFromJson(*settings, save.settings);
    }

    auto game = gameFromNative(save);
    if (!game) return {std::nullopt, game.error};
    return {std::move(save), {}};
}

NativeSave nativeFromGame(const chess::Game& game, const NativeSave& fields, int currentPly) {
    NativeSave save = fields;
    save.version = kNativeVersion;
    save.startFen = game.startPosition().fen();
    save.moves.clear();

    chess::Position position = game.startPosition();
    for (const chess::Move& m : game.moves()) {
        save.moves.push_back(position.longAlgebraic(m));
        position = position.apply(m);
    }
    save.currentPly =
        currentPly < 0 ? static_cast<int>(save.moves.size())
                       : std::min(currentPly, static_cast<int>(save.moves.size()));
    return save;
}

Result<chess::Game> gameFromNative(const NativeSave& save) {
    auto start = chess::Position::fromFen(save.startFen);
    if (!start) {
        return {std::nullopt, "the start position is not a FEN: " + save.startFen};
    }
    chess::Game game(*start);
    for (std::size_t i = 0; i < save.moves.size(); ++i) {
        auto move = game.position().parseLongAlgebraic(save.moves[i]);
        if (!move || !game.play(*move)) {
            return {std::nullopt, "move " + std::to_string(i + 1) + ", " + save.moves[i] +
                                      ", is not legal in the position it reaches"};
        }
    }
    if (save.currentPly < 0 || save.currentPly > static_cast<int>(save.moves.size())) {
        return {std::nullopt, "the current ply is outside the move list"};
    }
    return {std::move(game), {}};
}

}  // namespace swchess::save
