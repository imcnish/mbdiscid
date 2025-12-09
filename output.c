/*
 * mbdiscid - Disc ID calculator
 * output.c - Output formatting
 */

#include "output.h"
#include "toc.h"
#include "discid.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Print section header for All mode
 */
void output_section_header(const char *name)
{
    printf("----- %s -----\n", name);
}

/*
 * Get disc type name
 */
static const char *disc_type_name(disc_type_t type)
{
    switch (type) {
    case DISC_TYPE_AUDIO:
        return "Audio CD";
    case DISC_TYPE_ENHANCED:
        return "Enhanced CD";
    case DISC_TYPE_MIXED:
        return "Mixed Mode CD";
    default:
        return "Unknown";
    }
}

/*
 * Get disc type technical name
 */
static const char *disc_type_tech_name(disc_type_t type)
{
    switch (type) {
    case DISC_TYPE_AUDIO:
        return "CD-DA";
    case DISC_TYPE_ENHANCED:
        return "CD-Extra";
    case DISC_TYPE_MIXED:
        return "Mixed Mode";
    default:
        return "Unknown";
    }
}

/*
 * Output Type mode
 */
void output_type(const disc_info_t *disc)
{
    printf("%s\n", disc_type_name(disc->type));
    printf("%s\n", disc_type_tech_name(disc->type));
    printf("%d tracks\n", disc->toc.track_count);
    
    /* Track breakdown for non-standard CDs */
    if (disc->type == DISC_TYPE_ENHANCED || disc->type == DISC_TYPE_MIXED) {
        printf("%d audio track%s, %d data track%s\n",
               disc->toc.audio_count, disc->toc.audio_count == 1 ? "" : "s",
               disc->toc.data_count, disc->toc.data_count == 1 ? "" : "s");
    }
}

/*
 * Output Text mode (CD-Text)
 */
void output_text(const disc_info_t *disc)
{
    if (!disc->has_cdtext)
        return;
    
    const cdtext_t *text = &disc->cdtext;
    bool has_album_fields = false;
    
    /* Album-scope fields */
    if (text->album.album) {
        printf("ALBUM: %s\n", text->album.album);
        has_album_fields = true;
    }
    if (text->album.albumartist) {
        printf("ALBUMARTIST: %s\n", text->album.albumartist);
        has_album_fields = true;
    }
    if (text->album.lyricist) {
        printf("LYRICIST: %s\n", text->album.lyricist);
        has_album_fields = true;
    }
    if (text->album.composer) {
        printf("COMPOSER: %s\n", text->album.composer);
        has_album_fields = true;
    }
    if (text->album.arranger) {
        printf("ARRANGER: %s\n", text->album.arranger);
        has_album_fields = true;
    }
    if (text->album.genre) {
        printf("GENRE: %s\n", text->album.genre);
        has_album_fields = true;
    }
    if (text->album.comment) {
        printf("COMMENT: %s\n", text->album.comment);
        has_album_fields = true;
    }
    
    /* Track-scope fields */
    bool first_track = true;
    for (int i = 0; i < text->track_count; i++) {
        const cdtext_track_t *t = &text->tracks[i];
        
        /* Check if track has any fields */
        if (!t->title && !t->artist && !t->lyricist &&
            !t->composer && !t->arranger && !t->comment)
            continue;
        
        /* Blank line separator */
        if (has_album_fields || !first_track)
            printf("\n");
        first_track = false;
        
        printf("%d:\n", i + 1);
        
        if (t->title)
            printf("TITLE: %s\n", t->title);
        if (t->artist)
            printf("ARTIST: %s\n", t->artist);
        if (t->lyricist)
            printf("LYRICIST: %s\n", t->lyricist);
        if (t->composer)
            printf("COMPOSER: %s\n", t->composer);
        if (t->arranger)
            printf("ARRANGER: %s\n", t->arranger);
        if (t->comment)
            printf("COMMENT: %s\n", t->comment);
    }
}

/*
 * Output MCN mode
 */
void output_mcn(const disc_info_t *disc)
{
    if (disc->has_mcn)
        printf("%s\n", disc->ids.mcn);
}

/*
 * Output ISRC mode
 */
void output_isrc(const disc_info_t *disc)
{
    if (!disc->has_isrc)
        return;
    
    for (int i = 0; i < disc->toc.track_count; i++) {
        const track_t *t = &disc->toc.tracks[i];
        if (t->isrc[0] != '\0') {
            printf("%d: %s\n", t->number, t->isrc);
        }
    }
}

/*
 * Output Raw TOC
 */
void output_raw_toc(const toc_t *toc)
{
    char *str = toc_format_raw(toc);
    printf("%s\n", str);
    free(str);
}

/*
 * Output AccurateRip TOC
 */
void output_accuraterip_toc(const toc_t *toc)
{
    char *str = toc_format_accuraterip(toc);
    printf("%s\n", str);
    free(str);
}

/*
 * Output AccurateRip ID
 */
void output_accuraterip_id(const char *id)
{
    printf("%s\n", id);
}

/*
 * Output FreeDB TOC
 */
void output_freedb_toc(const toc_t *toc)
{
    char *str = toc_format_freedb(toc);
    printf("%s\n", str);
    free(str);
}

/*
 * Output FreeDB ID
 */
void output_freedb_id(const char *id)
{
    printf("%s\n", id);
}

/*
 * Output MusicBrainz TOC
 */
void output_musicbrainz_toc(const toc_t *toc)
{
    char *str = toc_format_musicbrainz(toc);
    printf("%s\n", str);
    free(str);
}

/*
 * Output MusicBrainz ID
 */
void output_musicbrainz_id(const char *id)
{
    printf("%s\n", id);
}

/*
 * Output MusicBrainz URL
 */
void output_musicbrainz_url(const char *url)
{
    printf("%s\n", url);
}

/*
 * Output All mode with headers
 */
void output_all(const disc_info_t *disc, const options_t *opts)
{
    bool need_separator = false;
    
    /* Media section */
    output_section_header("Media");
    output_type(disc);
    need_separator = true;
    
    /* Text section (if present) */
    if (disc->has_cdtext) {
        if (need_separator) printf("\n");
        output_section_header("Text");
        output_text(disc);
        need_separator = true;
    }
    
    /* MCN section (if present) */
    if (disc->has_mcn) {
        if (need_separator) printf("\n");
        output_section_header("MCN");
        output_mcn(disc);
        need_separator = true;
    }
    
    /* ISRC section (if present) */
    if (disc->has_isrc) {
        if (need_separator) printf("\n");
        output_section_header("ISRC");
        output_isrc(disc);
        need_separator = true;
    }
    
    /* Raw section */
    if (need_separator) printf("\n");
    output_section_header("Raw");
    output_raw_toc(&disc->toc);
    need_separator = true;
    
    /* AccurateRip section */
    if (need_separator) printf("\n");
    output_section_header("AccurateRip");
    if (opts->actions & ACTION_TOC)
        output_accuraterip_toc(&disc->toc);
    if (opts->actions & ACTION_ID)
        output_accuraterip_id(disc->ids.accuraterip);
    need_separator = true;
    
    /* FreeDB section */
    if (need_separator) printf("\n");
    output_section_header("FreeDB");
    if (opts->actions & ACTION_TOC)
        output_freedb_toc(&disc->toc);
    if (opts->actions & ACTION_ID)
        output_freedb_id(disc->ids.freedb);
    need_separator = true;
    
    /* MusicBrainz section */
    if (need_separator) printf("\n");
    output_section_header("MusicBrainz");
    if (opts->actions & ACTION_TOC)
        output_musicbrainz_toc(&disc->toc);
    if (opts->actions & ACTION_ID)
        output_musicbrainz_id(disc->ids.musicbrainz);
    if (opts->actions & ACTION_URL) {
        char *url = get_musicbrainz_url(disc->ids.musicbrainz);
        output_musicbrainz_url(url);
        free(url);
    }
}

/*
 * Open URL in browser
 */
int output_open_url(const char *url)
{
    char cmd[2048];
    
#ifdef PLATFORM_MACOS
    snprintf(cmd, sizeof(cmd), "open '%s' 2>/dev/null", url);
#else
    snprintf(cmd, sizeof(cmd), "xdg-open '%s' 2>/dev/null", url);
#endif
    
    int ret = system(cmd);
    return (ret == 0) ? 0 : 1;
}
