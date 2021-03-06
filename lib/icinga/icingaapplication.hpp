/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012-2018 Icinga Development Team (https://www.icinga.com/)  *
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

#ifndef ICINGAAPPLICATION_H
#define ICINGAAPPLICATION_H

#include "icinga/i2-icinga.hpp"
#include "icinga/icingaapplication-ti.hpp"
#include "icinga/macroresolver.hpp"

namespace icinga
{

/**
 * The Icinga application.
 *
 * @ingroup icinga
 */
class IcingaApplication final : public ObjectImpl<IcingaApplication>, public MacroResolver
{
public:
	DECLARE_OBJECT(IcingaApplication);
	DECLARE_OBJECTNAME(IcingaApplication);

	static void StaticInitialize();

	int Main() override;

	static void StatsFunc(const Dictionary::Ptr& status, const Array::Ptr& perfdata);

	static IcingaApplication::Ptr GetInstance();

	bool ResolveMacro(const String& macro, const CheckResult::Ptr& cr, Value *result) const override;

	String GetNodeName() const;

	String GetEnvironment() const override;
	void SetEnvironment(const String& value, bool suppress_events = false, const Value& cookie = Empty) override;

	void ValidateVars(const Lazy<Dictionary::Ptr>& lvalue, const ValidationUtils& utils) override;

private:
	void DumpProgramState();
	void DumpModifiedAttributes();

	void OnShutdown() override;
};

}

#endif /* ICINGAAPPLICATION_H */
