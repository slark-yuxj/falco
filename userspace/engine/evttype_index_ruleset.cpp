// SPDX-License-Identifier: Apache-2.0
/*
Copyright (C) 2023 The Falco Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "evttype_index_ruleset.h"

#include "falco_utils.h"

#include "logger.h"

#include <algorithm>

evttype_index_ruleset::evttype_index_ruleset(
	std::shared_ptr<sinsp_filter_factory> f): m_filter_factory(f)
{
}

evttype_index_ruleset::~evttype_index_ruleset()
{
}

evttype_index_ruleset::ruleset_filters::ruleset_filters()
{
}

evttype_index_ruleset::ruleset_filters::~ruleset_filters()
{
}

void evttype_index_ruleset::ruleset_filters::add_wrapper_to_list(filter_wrapper_list &wrappers, std::shared_ptr<filter_wrapper> wrap)
{
	// This is O(n) but it's also uncommon
	// (when loading rules only).
	auto pos = std::find(wrappers.begin(),
			     wrappers.end(),
			     wrap);

	if(pos == wrappers.end())
	{
		wrappers.push_back(wrap);
	}
}

void evttype_index_ruleset::ruleset_filters::remove_wrapper_from_list(filter_wrapper_list &wrappers, std::shared_ptr<filter_wrapper> wrap)
{
	// This is O(n) but it's also uncommon
	// (when loading rules only).
	auto pos = std::find(wrappers.begin(),
			     wrappers.end(),
			     wrap);
	if(pos != wrappers.end())
	{
		wrappers.erase(pos);
	}
}

void evttype_index_ruleset::ruleset_filters::add_filter(std::shared_ptr<filter_wrapper> wrap)
{
	if(wrap->event_codes.empty())
	{
		// Should run for all event types
		add_wrapper_to_list(m_filter_all_event_types, wrap);
	}
	else
	{
		for(auto &etype : wrap->event_codes)
		{
			if(m_filter_by_event_type.size() <= etype)
			{
				m_filter_by_event_type.resize(etype + 1);
			}

			add_wrapper_to_list(m_filter_by_event_type[etype], wrap);
		}
	}

	m_filters.insert(wrap);
}

void evttype_index_ruleset::ruleset_filters::remove_filter(std::shared_ptr<filter_wrapper> wrap)
{
	if(wrap->event_codes.empty())
	{
		remove_wrapper_from_list(m_filter_all_event_types, wrap);
	}
	else
	{
		for(auto &etype : wrap->event_codes)
		{
			if( etype < m_filter_by_event_type.size() )
			{
				remove_wrapper_from_list(m_filter_by_event_type[etype], wrap);
			}
		}
	}

	m_filters.erase(wrap);
}

uint64_t evttype_index_ruleset::ruleset_filters::num_filters()
{
	return m_filters.size();
}

bool evttype_index_ruleset::ruleset_filters::run(sinsp_evt *evt, falco_rule& match)
{
    if(evt->get_type() < m_filter_by_event_type.size())
    {
        for(const auto &wrap : m_filter_by_event_type[evt->get_type()])
        {
            if(wrap->filter->run(evt))
            {
				match = wrap->rule;
                return true;
            }
        }
    }

	// Finally, try filters that are not specific to an event type.
	for(const auto &wrap : m_filter_all_event_types)
	{
		if(wrap->filter->run(evt))
		{
			match = wrap->rule;
			return true;
		}
	}

	return false;
}

bool evttype_index_ruleset::ruleset_filters::run(sinsp_evt *evt, std::vector<falco_rule>& matches)
{
	bool match_found = false;

	if(evt->get_type() < m_filter_by_event_type.size())
	{
		for(const auto &wrap : m_filter_by_event_type[evt->get_type()])
		{
			if(wrap->filter->run(evt))
			{
				matches.push_back(wrap->rule);
				match_found = true;
			}
		}
	}

	if(match_found)
	{
		return true;
	}

	// Finally, try filters that are not specific to an event type.
	for(const auto &wrap : m_filter_all_event_types)
	{
		if(wrap->filter->run(evt))
		{
			matches.push_back(wrap->rule);
			match_found = true;
		}
	}

	return match_found;
}

libsinsp::events::set<ppm_sc_code> evttype_index_ruleset::ruleset_filters::sc_codes()
{
	libsinsp::events::set<ppm_sc_code> res;
	for(const auto &wrap : m_filters)
	{
		res.insert(wrap->sc_codes.begin(), wrap->sc_codes.end());
	}
	return res;
}

libsinsp::events::set<ppm_event_code> evttype_index_ruleset::ruleset_filters::event_codes()
{
	libsinsp::events::set<ppm_event_code> res;
	for(const auto &wrap : m_filters)
	{
		res.insert(wrap->event_codes.begin(), wrap->event_codes.end());
	}
	return res;
}

void evttype_index_ruleset::add(
		const falco_rule& rule,
		std::shared_ptr<sinsp_filter> filter,
		std::shared_ptr<libsinsp::filter::ast::expr> condition)
{
	try
	{
		auto wrap = std::make_shared<filter_wrapper>();
		wrap->rule = rule;
		wrap->filter = filter;
		if(rule.source == falco_common::syscall_source)
		{
			wrap->sc_codes = libsinsp::filter::ast::ppm_sc_codes(condition.get());
			wrap->event_codes = libsinsp::filter::ast::ppm_event_codes(condition.get());
		}
		else
		{
			wrap->sc_codes = { };
			wrap->event_codes = { ppm_event_code::PPME_PLUGINEVENT_E };
		}
		wrap->event_codes.insert(ppm_event_code::PPME_ASYNCEVENT_E);
		m_filters.insert(wrap);
	}
	catch (const sinsp_exception& e)
	{
		throw falco_exception(std::string(e.what()));
	}
}

void evttype_index_ruleset::on_loading_complete()
{
	print_enabled_rules_falco_logger();
}

void evttype_index_ruleset::print_enabled_rules_falco_logger()
{
	falco_logger::log(falco_logger::level::DEBUG, "Enabled rules:\n");
	int n = 0;
	for (const auto& ruleset_ptr : m_rulesets)
	{
		if (ruleset_ptr)
		{
			for (const auto& wrap : ruleset_ptr->get_filters())
			{
				n++;
				falco_logger::log(falco_logger::level::DEBUG, std::string("   ") + wrap->rule.name + "\n");
			}
		}
	}
	falco_logger::log(falco_logger::level::DEBUG, "(" + std::to_string(n) + ") enabled rules in total\n");
}

void evttype_index_ruleset::clear()
{
	for (size_t i = 0; i < m_rulesets.size(); i++)
	{
		m_rulesets[i] = std::make_shared<ruleset_filters>();
	}
	m_filters.clear();
}

void evttype_index_ruleset::enable(const std::string &pattern, match_type match, uint16_t ruleset_id)
{
	enable_disable(pattern, match, true, ruleset_id);
}

void evttype_index_ruleset::disable(const std::string &pattern, match_type match, uint16_t ruleset_id)
{
	enable_disable(pattern, match, false, ruleset_id);
}

void evttype_index_ruleset::enable_disable(const std::string &pattern, match_type match, bool enabled, uint16_t ruleset_id)
{
	while(m_rulesets.size() < (size_t)ruleset_id + 1)
	{
		m_rulesets.emplace_back(std::make_shared<ruleset_filters>());
	}

	for(const auto &wrap : m_filters)
	{
		bool matches;
		std::string::size_type pos;

		switch(match)
		{
		case match_type::exact:
			pos = wrap->rule.name.find(pattern);

			matches = (pattern == "" || (pos == 0 &&
						       pattern.size() == wrap->rule.name.size()));
			break;
		case match_type::substring:
			matches = (pattern == "" || (wrap->rule.name.find(pattern) != std::string::npos));
			break;
		case match_type::wildcard:
			matches = falco::utils::matches_wildcard(pattern, wrap->rule.name);
			break;
		default:
			// should never happen
			matches = false;
		}

		if(matches)
		{
			if(enabled)
			{
				m_rulesets[ruleset_id]->add_filter(wrap);
			}
			else
			{
				m_rulesets[ruleset_id]->remove_filter(wrap);
			}
		}
	}
}

void evttype_index_ruleset::enable_tags(const std::set<std::string> &tags, uint16_t ruleset_id)
{
	enable_disable_tags(tags, true, ruleset_id);
}

void evttype_index_ruleset::disable_tags(const std::set<std::string> &tags, uint16_t ruleset_id)
{
	enable_disable_tags(tags, false, ruleset_id);
}

void evttype_index_ruleset::enable_disable_tags(const std::set<std::string> &tags, bool enabled, uint16_t ruleset_id)
{
	while(m_rulesets.size() < (size_t)ruleset_id + 1)
	{
		m_rulesets.emplace_back(std::make_shared<ruleset_filters>());
	}

	for(const auto &wrap : m_filters)
	{
		std::set<std::string> intersect;

		set_intersection(tags.begin(), tags.end(),
				 wrap->rule.tags.begin(), wrap->rule.tags.end(),
				 inserter(intersect, intersect.begin()));

		if(!intersect.empty())
		{
			if(enabled)
			{
				m_rulesets[ruleset_id]->add_filter(wrap);
			}
			else
			{
				m_rulesets[ruleset_id]->remove_filter(wrap);
			}
		}
	}
}

uint64_t evttype_index_ruleset::enabled_count(uint16_t ruleset_id)
{
	while(m_rulesets.size() < (size_t)ruleset_id + 1)
	{
		m_rulesets.emplace_back(std::make_shared<ruleset_filters>());
	}

	return m_rulesets[ruleset_id]->num_filters();
}

bool evttype_index_ruleset::run(sinsp_evt *evt, falco_rule& match, uint16_t ruleset_id)
{
	if(m_rulesets.size() < (size_t)ruleset_id + 1)
	{
		return false;
	}

	return m_rulesets[ruleset_id]->run(evt, match);
}

bool evttype_index_ruleset::run(sinsp_evt *evt, std::vector<falco_rule>& matches, uint16_t ruleset_id)
{
	if(m_rulesets.size() < (size_t)ruleset_id + 1)
	{
		return false;
	}

	return m_rulesets[ruleset_id]->run(evt, matches);
}

void evttype_index_ruleset::enabled_evttypes(std::set<uint16_t> &evttypes, uint16_t ruleset_id)
{
	evttypes.clear();
	for (const auto& e : enabled_event_codes(ruleset_id))
	{
		evttypes.insert((uint16_t) e);
	}
}

libsinsp::events::set<ppm_sc_code> evttype_index_ruleset::enabled_sc_codes(uint16_t ruleset)
{
	if(m_rulesets.size() < (size_t)ruleset + 1)
	{
		return {};
	}
	return m_rulesets[ruleset]->sc_codes();
}

libsinsp::events::set<ppm_event_code> evttype_index_ruleset::enabled_event_codes(uint16_t ruleset)
{
	if(m_rulesets.size() < (size_t)ruleset + 1)
	{
		return {};
	}
	return m_rulesets[ruleset]->event_codes();
}
