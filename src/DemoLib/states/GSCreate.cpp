#include "GSCreate.h"

#include <stdexcept>

#include "../Viewer.h"
#include "GSFilterTest.h"
#include "GSHDRTest.h"
#include "GSHybTest.h"
#include "GSLightmapTest.h"
#include "GSNoiseTest.h"
#include "GSRayTest.h"
#include "GSSamplingTest.h"
#include "GSVNDFTest.h"

std::shared_ptr<GameState> GSCreate(eGameState state, Viewer *game) {
    switch (state) {
    case GS_RAY_TEST:
        return std::make_shared<GSRayTest>(game);
    case GS_SAMPLING_TEST:
        return std::make_shared<GSSamplingTest>(game);
    case GS_HYB_TEST:
        return std::make_shared<GSHybTest>(game);
    case GS_HDR_TEST:
        return std::make_shared<GSHDRTest>(game);
    case GS_LM_TEST:
        return std::make_shared<GSLightmapTest>(game);
    case GS_VNDF_TEST:
        return std::make_shared<GSVNDFTest>(game);
    case GS_FILTER_TEST:
        return std::make_shared<GSFilterTest>(game);
    case GS_NOISE_TEST:
        return std::make_shared<GSNoiseTest>(game);
    default:
        throw std::invalid_argument("Unknown game state!");
    }
}
