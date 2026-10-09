#include "RenderGraphCompile.h"

#include <format>

namespace render::rg
{

namespace
{

void apply(Kind kind, uint32_t resource, Tracked &t, const ResourceState &need, std::vector<BarrierRecord> &out)
{
    const bool writing      = (need.access & WriteMask) != 0;
    const bool layoutChange = kind == Kind::Texture && t.layout != need.layout;

    if (writing || layoutChange) {
        out.push_back(BarrierRecord{ .kind = kind, .resource = resource,
                                     .src = { t.writeStages | t.syncedStages, t.writeAccess, t.layout },
                                     .dst = need });
        t.writeStages  = need.stage;
        t.writeAccess  = need.access & WriteMask;
        t.syncedStages = need.stage;
        t.syncedAccess = need.access;
        t.layout       = need.layout;
        return;
    }

    const bool covered = (need.stage & ~t.syncedStages) == 0 && (need.access & ~t.syncedAccess) == 0;
    if (!covered && t.writeStages != VK_PIPELINE_STAGE_2_NONE) {
        out.push_back(BarrierRecord{ .kind = kind, .resource = resource,
                                     .src = { t.writeStages, t.writeAccess, t.layout },
                                     .dst = need });
    }
    t.syncedStages |= need.stage;
    t.syncedAccess |= need.access;
}

uint32_t barrierCount(const BarrierPlan &plan)
{
    return static_cast<uint32_t>(plan.barriers.size());
}

}

Tracked trackedFrom(const ResourceState &state)
{
    return Tracked{ .writeStages  = state.stage,
                    .writeAccess  = state.access & WriteMask,
                    .syncedStages = state.stage,
                    .syncedAccess = state.access,
                    .layout       = state.layout };
}

bool loadsContents(const PassDecl &pass, const UseDecl &use)
{
    if (!writes(use.usage) || use.usage == Usage::ColorReadWrite) {
        return true;
    }
    return use.kind == Kind::Texture && pass.depth && pass.depth->texture.index == use.resource
        && pass.depth->load == LoadOp::Load;
}

void compileBarriers(std::span<const ResourceDecl> resources, std::span<const PassDecl> passes,
                     std::span<const Tracked> initial, BarrierPlan &plan)
{
    plan.barriers.clear();
    plan.passes.assign(passes.size(), BarrierRange{});
    plan.final = BarrierRange{};
    plan.errors.clear();
    plan.states.assign(initial.begin(), initial.end());

    std::vector<uint8_t> written(resources.size(), 0);

    for (size_t p = 0; p < passes.size(); ++p) {
        const PassDecl &pass = passes[p];
        plan.passes[p].first = barrierCount(plan);

        for (const UseDecl &use : pass.uses) {
            const ResourceDecl &decl = resources[use.resource];
            if (!decl.imported && !written[use.resource] && loadsContents(pass, use)) {
                plan.errors.push_back(std::format("Pass '{}' reads '{}' before any pass writes it this frame",
                                                  pass.name, decl.name));
            }
            if (writes(use.usage)) {
                written[use.resource] = 1;
            }

            ResourceState need = stateFor(use.usage);
            if (use.kind == Kind::Buffer) {
                need.layout = VK_IMAGE_LAYOUT_UNDEFINED;
            }
            apply(use.kind, use.resource, plan.states[use.resource], need, plan.barriers);
        }
        plan.passes[p].count = barrierCount(plan) - plan.passes[p].first;
    }

    plan.final.first = barrierCount(plan);
    for (size_t r = 0; r < resources.size(); ++r) {
        if (resources[r].final) {
            apply(resources[r].kind, static_cast<uint32_t>(r), plan.states[r], *resources[r].final, plan.barriers);
        }
    }
    plan.final.count = barrierCount(plan) - plan.final.first;
}

}
