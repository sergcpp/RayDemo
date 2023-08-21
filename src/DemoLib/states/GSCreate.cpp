#include "GSCreate.h"

#include <stdexcept>

#include "GSHDRTest.h"
#include "GSHybTest.h"
#include "GSRayTest.h"
#include "GSLightmapTest.h"
#include "GSSamplingTest.h"
#include "GSVNDFTest.h"

std::shared_ptr<GameState> GSCreate(eGameState state, GameBase *game) {
    if (state == GS_RAY_TEST) {
        return std::make_shared<GSRayTest>(game);
    } else if (state == GS_SAMPLING_TEST) {
        return std::make_shared<GSSamplingTest>(game);
    } else if (state == GS_HYB_TEST) {
        return std::make_shared<GSHybTest>(game);
    } else if (state == GS_HDR_TEST) {
        return std::make_shared<GSHDRTest>(game);
    } else if (state == GS_LM_TEST) {
        return std::make_shared<GSLightmapTest>(game);
    } else if (state == GS_VNDF_TEST) {
        return std::make_shared<GSVNDFTest>(game);
    }
    throw std::invalid_argument("Unknown game state!");
}
