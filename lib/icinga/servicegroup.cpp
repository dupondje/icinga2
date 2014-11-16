/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012-2014 Icinga Development Team (http://www.icinga.org)    *
 *                                                                            *
 * This program is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU General Public License                *
 * as published by the Free Software Foundation; either version 2             *
 * of the License, or (at your option) any later version.                     *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software Foundation     *
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ******************************************************************************/

#include "icinga/servicegroup.hpp"
#include "config/objectrule.hpp"
#include "config/configitem.hpp"
#include "base/dynamictype.hpp"
#include "base/objectlock.hpp"
#include "base/logger.hpp"
#include "base/context.hpp"
#include "base/workqueue.hpp"
#include <boost/foreach.hpp>

using namespace icinga;

REGISTER_TYPE(ServiceGroup);

INITIALIZE_ONCE(&ServiceGroup::RegisterObjectRuleHandler);

void ServiceGroup::RegisterObjectRuleHandler(void)
{
	ObjectRule::RegisterType("ServiceGroup");
}

bool ServiceGroup::EvaluateObjectRule(const Service::Ptr& service, const ConfigItem::Ptr& group)
{
	String group_name = group->GetName();

	CONTEXT("Evaluating rule for group '" + group_name + "'");

	Host::Ptr host = service->GetHost();

	Dictionary::Ptr locals = new Dictionary();
	locals->Set("__parent", group->GetScope());
	locals->Set("host", host);
	locals->Set("service", service);

	if (!group->GetFilter()->Evaluate(locals))
		return false;

	Log(LogDebug, "ServiceGroup")
	    << "Assigning membership for group '" << group_name << "' to service '" << service->GetName() << "'";

	ServiceGroup::Ptr groupObject = ServiceGroup::GetByName(group_name);

	if (!groupObject) {
		Log(LogCritical, "ServiceGroup")
		    << "Invalid membership assignment. Group '" << group_name << "' does not exist.";
		return false;
	}

	/* assign service group membership */
	groupObject->ResolveGroupMembership(service, true);

	return true;
}

void ServiceGroup::EvaluateObjectRules(const Service::Ptr& service)
{
	CONTEXT("Evaluating group membership for service '" + service->GetName() + "'");

	BOOST_FOREACH(const ConfigItem::Ptr& group, ConfigItem::GetItems("ServiceGroup"))
	{
		if (!group->GetFilter())
			continue;

		EvaluateObjectRule(service, group);
	}
}

std::set<Service::Ptr> ServiceGroup::GetMembers(void) const
{
	boost::mutex::scoped_lock lock(m_ServiceGroupMutex);
	return m_Members;
}

void ServiceGroup::AddMember(const Service::Ptr& service)
{
	service->AddGroup(GetName());

	boost::mutex::scoped_lock lock(m_ServiceGroupMutex);
	m_Members.insert(service);
}

void ServiceGroup::RemoveMember(const Service::Ptr& service)
{
	boost::mutex::scoped_lock lock(m_ServiceGroupMutex);
	m_Members.erase(service);
}

bool ServiceGroup::ResolveGroupMembership(const Service::Ptr& service, bool add, int rstack) {

	if (add && rstack > 20) {
		Log(LogWarning, "ServiceGroup")
		    << "Too many nested groups for group '" << GetName() << "': Service '"
		    << service->GetName() << "' membership assignment failed.";

		return false;
	}

	Array::Ptr groups = GetGroups();

	if (groups && groups->GetLength() > 0) {
		ObjectLock olock(groups);

		BOOST_FOREACH(const String& name, groups) {
			ServiceGroup::Ptr group = ServiceGroup::GetByName(name);

			if (group && !group->ResolveGroupMembership(service, add, rstack + 1))
				return false;
		}
	}

	if (add)
		AddMember(service);
	else
		RemoveMember(service);

	return true;
}
