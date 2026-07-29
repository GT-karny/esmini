#include "gt_esmini/control/common/DomainOwnershipLedger.hpp"

namespace gt_esmini
{

namespace
{
/// Map an OwnedDomain onto its esmini ControlDomainMasks bit.
unsigned int DomainBit(OwnedDomain domain)
{
    return domain == OwnedDomain::LATERAL ? static_cast<unsigned int>(ControlDomainMasks::DOMAIN_MASK_LAT)
                                          : static_cast<unsigned int>(ControlDomainMasks::DOMAIN_MASK_LONG);
}
}  // namespace

const char* OwnedDomainToStr(OwnedDomain domain)
{
    return domain == OwnedDomain::LATERAL ? "lateral" : "longitudinal";
}

DomainOwnershipLedger& DomainOwnershipLedger::Instance()
{
    static DomainOwnershipLedger instance;
    return instance;
}

void DomainOwnershipLedger::Claim(int object_id, const void* controller, const std::string& controller_name, unsigned int domain_mask)
{
    if (!controller)
    {
        return;
    }

    ObjectSlots& slots = objects_[object_id];

    for (unsigned int i = 0; i < static_cast<unsigned int>(OwnedDomain::COUNT); i++)
    {
        Slot&       slot   = slots.slot[i];
        const bool  wanted = (domain_mask & DomainBit(static_cast<OwnedDomain>(i))) != 0;

        if (wanted)
        {
            // Last claimer wins — this is the eviction that upstream's
            // per-domain deactivation fails to perform (see header).
            slot.owner = controller;
            slot.name  = controller_name;
        }
        else if (slot.owner == controller)
        {
            // Give up only what we hold. Omitting a domain never evicts a peer,
            // which is what makes the outcome independent of activation order.
            slot.owner = nullptr;
            slot.name.clear();
        }
    }
}

void DomainOwnershipLedger::ReleaseAll(int object_id, const void* controller)
{
    auto it = objects_.find(object_id);
    if (it == objects_.end())
    {
        return;
    }

    for (unsigned int i = 0; i < static_cast<unsigned int>(OwnedDomain::COUNT); i++)
    {
        Slot& slot = it->second.slot[i];
        if (slot.owner == controller)
        {
            slot.owner = nullptr;
            slot.name.clear();
        }
    }
}

const DomainOwnershipLedger::Slot* DomainOwnershipLedger::Find(int object_id, OwnedDomain domain) const
{
    auto it = objects_.find(object_id);
    if (it == objects_.end())
    {
        return nullptr;
    }
    return &it->second.slot[static_cast<unsigned int>(domain)];
}

bool DomainOwnershipLedger::IsOwner(int object_id, const void* controller, OwnedDomain domain) const
{
    const Slot* slot = Find(object_id, domain);
    return slot != nullptr && slot->owner == controller;
}

bool DomainOwnershipLedger::HasOwner(int object_id, OwnedDomain domain) const
{
    const Slot* slot = Find(object_id, domain);
    return slot != nullptr && slot->owner != nullptr;
}

const void* DomainOwnershipLedger::OwnerOf(int object_id, OwnedDomain domain) const
{
    const Slot* slot = Find(object_id, domain);
    return slot ? slot->owner : nullptr;
}

std::string DomainOwnershipLedger::OwnerName(int object_id, OwnedDomain domain) const
{
    const Slot* slot = Find(object_id, domain);
    return (slot && slot->owner) ? slot->name : std::string();
}

std::string DomainOwnershipLedger::Describe(int object_id) const
{
    auto name_or_none = [&](OwnedDomain domain) {
        const std::string n = OwnerName(object_id, domain);
        return n.empty() ? std::string("<none>") : n;
    };

    return "obj=" + std::to_string(object_id) + " lat=" + name_or_none(OwnedDomain::LATERAL) +
           " lon=" + name_or_none(OwnedDomain::LONGITUDINAL);
}

void DomainOwnershipLedger::Clear()
{
    objects_.clear();
}

}  // namespace gt_esmini
