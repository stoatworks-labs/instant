/*
 * Stoatworks Labs - About window data for Instant.
 *
 * PROVISIONAL HAND COPY, written 2026-09-25 in the shape that
 * stoatworks-backend/scripts/sync-about.py generates, because Instant is not
 * in the website's projects.json yet. When it is registered, the sync
 * overwrites this file; until then, edit it by hand. `guide` is empty on
 * purpose: no user guide exists, and an empty link is left out of the About
 * buttons rather than shown as one that opens a 404.
 *
 * `version` here is a fallback read from this repo's own manifest at sync
 * time. Anything with a build step injects the real one at build time and
 * overrides this.
 */
#pragma once

namespace stoatworks::about
{
    inline constexpr auto name = "Instant";
    inline constexpr auto slug = "instant";
    inline constexpr auto hook = "Instant film developing in front of you, for Resolume";
    inline constexpr auto licence = "MIT";
    inline constexpr auto guide = "";
    inline constexpr auto page = "https://stoatworks-labs.com/software/instant/";
    inline constexpr auto repo = "https://github.com/stoatworks-labs/instant";
    inline constexpr auto versionFallback = "v0.1.0";

    inline constexpr auto org = "Stoatworks Labs";
    inline constexpr auto home = "https://stoatworks-labs.com";
    inline constexpr auto tagline = "Open tools for the people who run the show.";

    /* The canonical funding links, matching FUNDING.yml and the support footer. */
    struct Link { const char* name; const char* url; };
    inline constexpr Link funding[] = {
        { "GitHub Sponsors", "https://github.com/sponsors/stoatworks-labs" },
        { "Ko-fi", "https://ko-fi.com/stoatworkslabs" },
        { "Patreon", "https://patreon.com/StoatworksLabs" },
        { "Liberapay", "https://liberapay.com/stoatworks-labs" },
    };
}
