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
 * Format disc size in human-readable form
 */
static void format_disc_size(int32_t leadout, char *buf, size_t bufsize)
{
    /* Calculate size in MB (2048 bytes per sector for data, but we show raw capacity) */
    /* Raw capacity: leadout frames * 2352 bytes per frame */
    double raw_mb = ((double)leadout * 2352.0) / (1024.0 * 1024.0);
    
    if (raw_mb >= 1024.0) {
        snprintf(buf, bufsize, "%.1fGB", raw_mb / 1024.0);
    } else {
        snprintf(buf, bufsize, "%.0fMB", raw_mb);
    }
}

/*
 * Output TOC table
 */
static void output_toc_table(const disc_info_t *disc)
{
    const toc_t *toc = &disc->toc;
    
    /* Table header */
    printf("\n");
    printf("         ----- Start -----  ----- Length -----\n");
    printf("S#  T#        MSF      LBA       MSF       LBA  Type   Ch  Pre\n");
    
    /* Track rows */
    for (int i = 0; i < toc->track_count; i++) {
        const track_t *t = &toc->tracks[i];
        
        int start_m, start_s, start_f;
        int len_m, len_s, len_f;
        
        /* Convert LBA to MSF (add 150 frames for 2-second offset) */
        int32_t start_with_pregap = t->offset + PREGAP_FRAMES;
        lba_to_msf(start_with_pregap, &start_m, &start_s, &start_f);
        lba_to_msf(t->length, &len_m, &len_s, &len_f);
        
        const char *type_str = (t->type == TRACK_TYPE_AUDIO) ? "audio" : "data";
        const char *ch_str = (t->type == TRACK_TYPE_AUDIO) ? "2" : "-";
        
        /* Preemphasis: check control nibble bit 0 */
        const char *pre_str;
        if (t->type == TRACK_TYPE_AUDIO) {
            pre_str = (t->control & 0x01) ? "yes" : "no";
        } else {
            pre_str = "-";
        }
        
        printf("%2d  %2d   %02d:%02d:%02d  %7d  %02d:%02d:%02d  %8d  %-5s  %2s  %s\n",
               t->session, t->number,
               start_m, start_s, start_f, t->offset,
               len_m, len_s, len_f, t->length,
               type_str, ch_str, pre_str);
    }
    
    /* Leadout row */
    int lo_m, lo_s, lo_f;
    int32_t leadout_with_pregap = toc->leadout + PREGAP_FRAMES;
    lba_to_msf(leadout_with_pregap, &lo_m, &lo_s, &lo_f);
    
    char size_buf[16];
    format_disc_size(toc->leadout, size_buf, sizeof(size_buf));
    
    printf(" -  LO   %02d:%02d:%02d  %7d         -  %8s  -       -    -\n",
           lo_m, lo_s, lo_f, toc->leadout, size_buf);
}

/*
 * Output Type mode
 */
void output_type(const disc_info_t *disc)
{
    /* Disc type names */
    printf("%s\n", disc_type_name(disc->type));
    printf("%s\n", disc_type_tech_name(disc->type));
    
    /* Total track count (audio + data) */
    int total_tracks = disc->toc.audio_count + disc->toc.data_count;
    printf("%d track%s\n", total_tracks, total_tracks == 1 ? "" : "s");
    
    /* Track breakdown for non-standard CDs */
    if (disc->type == DISC_TYPE_ENHANCED || disc->type == DISC_TYPE_MIXED) {
        printf("%d audio track%s, %d data track%s\n",
               disc->toc.audio_count, disc->toc.audio_count == 1 ? "" : "s",
               disc->toc.data_count, disc->toc.data_count == 1 ? "" : "s");
    }
    
    /* TOC table */
    output_toc_table(disc);
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
