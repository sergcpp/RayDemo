#pragma once

#include <memory>

enum eGameState { GS_RAY_TEST, GS_SAMPLING_TEST, GS_HYB_TEST, GS_HDR_TEST, GS_LM_TEST, GS_VNDF_TEST, GS_FILTER_TEST };

class GameBase;
class GameState;
class Viewer;

std::shared_ptr<GameState> GSCreate(eGameState state, Viewer *game);
