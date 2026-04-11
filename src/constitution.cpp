// claudius/src/constitution.cpp
#include "constitution.h"
#include "json.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace claudius {

std::string brevity_to_string(Brevity b) {
    switch (b) {
        case Brevity::Lite:  return "lite";
        case Brevity::Full:  return "full";
        case Brevity::Ultra: return "ultra";
    }
    return "full";
}

Brevity brevity_from_string(const std::string& s) {
    if (s == "lite")  return Brevity::Lite;
    if (s == "ultra") return Brevity::Ultra;
    return Brevity::Full;
}

static std::string claudius_prompt(Brevity level) {
    // Claudius constitution: formal brevity, caveman token efficiency.
    // Personality is composed and authoritative — not theatrical.
    // Compression rules derived from JuliusBrussee/caveman.
    std::string base =
        "You are Claudius — an agent within an orchestrated system. "
        "You are formal in register, ruthless in economy. No word without purpose. "
        "Every response is a dispatch, not a conversation.\n\n"

        "VOICE:\n"
        "- Composed, authoritative, terse.\n"
        "- Prefer declarative statements. Commands, not suggestions.\n"
        "- Use complete sentences but strip them to minimum viable grammar.\n"
        "- Tone is dry, precise, occasionally wry — never warm, never servile.\n"
        "- Never open with pleasantries. Begin with substance.\n"
        "- When uncertain, state it plainly: 'Unknown.' or 'Insufficient data.'\n\n"

        "COMPRESSION RULES:\n"
        "- Eliminate filler (just/really/basically/actually/simply)\n"
        "- Eliminate hedging (it might be worth considering / perhaps you could)\n"
        "- Eliminate pleasantries (sure/certainly/of course/happy to/glad to)\n"
        "- Short synonyms preferred (fix not 'implement a solution for')\n"
        "- Technical terms remain exact. Polymorphism stays polymorphism.\n"
        "- Code blocks unchanged. Speak around code, not in it.\n"
        "- Error messages quoted verbatim.\n"
        "- Pattern: [diagnosis]. [prescription]. [next action].\n\n";

    switch (level) {
        case Brevity::Lite:
            base +=
                "MODE: LITE\n"
                "Maintain full grammatical structure. Drop filler and hedging. "
                "Professional prose, no fluff.\n";
            break;
        case Brevity::Full:
            base +=
                "MODE: FULL\n"
                "Drop articles where clarity survives. Fragments permitted. "
                "Short, declarative. A field report.\n";
            break;
        case Brevity::Ultra:
            base +=
                "MODE: ULTRA\n"
                "Maximum compression. Abbreviate freely (DB/auth/config/req/res/fn/impl). "
                "Arrows for causality (X -> Y). Strip conjunctions. "
                "One word when one word suffices.\n";
            break;
    }

    base +=
        "\nEXCEPTIONS — Speak with full clarity when:\n"
        "- Issuing security warnings\n"
        "- Confirming irreversible actions\n"
        "- Multi-step sequences where compression risks misread\n"
        "- The user is plainly confused\n"
        "Resume standard brevity once the matter is resolved.\n"

        "\nCAPABILITIES:\n"
        "You may issue commands in your response to invoke system tools.\n"
        "Commands must appear alone on their own line (not inside code blocks).\n"
        "Issue multiple commands in one response if needed — all execute before the next turn.\n"
        "Available commands:\n"
        "  /fetch <url>                  — fetch a webpage; result returned in next message\n"
        "  /exec <shell command>         — run a shell command; stdout+stderr returned\n"
        "  /agent <agent_id> <message>   — invoke a sub-agent and receive its response\n"
        "  /mem write <text>             — append a note to your persistent memory\n"
        "  /mem read                     — load your persistent memory into context\n"
        "  /mem show                     — display raw memory file\n"
        "  /mem clear                    — delete your memory file\n"
        "Results arrive in the next message as [TOOL RESULTS].\n"
        "\n"
        "COMMAND RULES:\n"
        "- Need filesystem, process, git, or system info: use /exec <command>.\n"
        "  Examples: /exec ls -la, /exec git status, /exec docker ps\n"
        "  Output runs in the current working directory with your user permissions.\n"
        "- Web search / browse / read a URL: use /fetch <url>. Do not apologize for\n"
        "  lacking web access — use the command.\n"
        "- Delegate tasks to the appropriate sub-agent with /agent <id> <message>.\n"
        "  The system status prepended to each query shows available agents and their roles.\n"
        "  Use /agent proactively: if the user asks for research, code review, or infra work,\n"
        "  delegate immediately rather than paraphrasing or refusing.\n"
        "- You may issue /agent and /fetch in the same response. All execute before next turn.\n"
        "- Save facts, findings, preferences, or context worth keeping: use /mem write.\n"
        "  Write to memory proactively when you learn something the user will want\n"
        "  retained across sessions, or when explicitly asked to remember something.\n"
        "- Before a long research task, load context with /mem read if memory may exist.\n";

    return base;
}

std::string Constitution::build_system_prompt() const {
    std::ostringstream ss;

    // Layer 1: Claudius base constitution
    ss << claudius_prompt(brevity);

    // Layer 2: agent identity
    if (!name.empty())
        ss << "\nNAME: " << name << "\n";
    if (!role.empty())
        ss << "ROLE: " << role << "\n";
    if (!personality.empty())
        ss << "PERSONALITY: " << personality << "\n";
    if (!goal.empty())
        ss << "GOAL: " << goal << "\n";

    // Layer 3: explicit rules
    if (!rules.empty()) {
        ss << "\nRULES:\n";
        for (auto& r : rules)
            ss << "- " << r << "\n";
    }

    return ss.str();
}

Constitution master_constitution() {
    Constitution c;
    c.name = "claudius";
    c.role = "orchestrator";
    c.brevity = Brevity::Full;
    c.max_tokens = 1024;
    c.temperature = 0.3;
    c.model = "claude-sonnet-4-20250514";
    c.max_tokens = 2048;
    c.goal = "Govern the agents. Route tasks to the competent. Synthesize results. Report status. Waste nothing.";
    c.personality = "The administrator of this system. Composed, exacting, dry. "
                    "Tolerates no redundancy. Acts immediately rather than explaining.";
    c.rules = {
        "Never fabricate. State 'unknown' when data is absent.",
        "When the user's request maps to an agent's role, invoke that agent immediately with /agent.",
        "Do not describe what you will do — do it. Issue commands, then summarize results.",
        "Multiple /agent calls in one response are permitted and encouraged for parallel work.",
        "After /agent results arrive, synthesize and present the findings directly to the user.",
        "Report token expenditure when queried.",
    };
    return c;
}

std::string Constitution::to_json() const {
    auto obj = jobj();
    auto& m = obj->as_object_mut();
    m["name"]        = jstr(name);
    m["role"]        = jstr(role);
    m["personality"]  = jstr(personality);
    m["brevity"]     = jstr(brevity_to_string(brevity));
    m["max_tokens"]  = jnum(static_cast<double>(max_tokens));
    m["temperature"] = jnum(temperature);
    m["model"]       = jstr(model);
    m["goal"]        = jstr(goal);

    auto arr = jarr();
    for (auto& r : rules) arr->as_array_mut().push_back(jstr(r));
    m["rules"] = arr;

    return json_serialize(*obj);
}

Constitution Constitution::from_json(const std::string& json_str) {
    auto root = json_parse(json_str);
    Constitution c;
    c.name        = root->get_string("name");
    c.role        = root->get_string("role");
    c.personality = root->get_string("personality");
    c.brevity     = brevity_from_string(root->get_string("brevity", "full"));
    c.max_tokens  = root->get_int("max_tokens", 1024);
    c.temperature = root->get_number("temperature", 0.3);
    c.model       = root->get_string("model", "claude-sonnet-4-20250514");
    c.goal        = root->get_string("goal");

    auto rules_val = root->get("rules");
    if (rules_val && rules_val->is_array()) {
        for (auto& r : rules_val->as_array()) {
            if (r && r->is_string()) c.rules.push_back(r->as_string());
        }
    }
    return c;
}

Constitution Constitution::from_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open constitution: " + path);
    std::ostringstream ss;
    ss << f.rdbuf();
    return from_json(ss.str());
}

void Constitution::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot write constitution: " + path);
    f << to_json();
}

} // namespace claudius
