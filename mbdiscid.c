#include <stdio.h>
#include <string.h>
#include <discid/discid.h>

void print_usage(char *prog) {
    fprintf(stderr, "Usage: %s [-i|-u|-t|-a] /dev/diskN\n", prog);
    fprintf(stderr, "  -i or no flag: MusicBrainz disc ID only (default)\n");
    fprintf(stderr, "  -u: Submission URL only\n");
    fprintf(stderr, "  -t: Full TOC info (ID + track offsets)\n");
    fprintf(stderr, "  -a: All output (TOC + URL)\n");
}

int main(int argc, char *argv[]) {
    int output_mode = 1;  // Default: disc ID only
    char *device;
    
    if (argc < 2 || argc > 3) {
        print_usage(argv[0]);
        return 1;
    }
    
    if (argc == 3) {
        if (strcmp(argv[1], "-i") == 0) output_mode = 1;
        else if (strcmp(argv[1], "-u") == 0) output_mode = 2;
        else if (strcmp(argv[1], "-t") == 0) output_mode = 3;
        else if (strcmp(argv[1], "-a") == 0) output_mode = 4;
        else {
            print_usage(argv[0]);
            return 1;
        }
        device = argv[2];
    } else {
        device = argv[1];
    }
    
    DiscId *disc = discid_new();
    
    if (!discid_read_sparse(disc, device, DISCID_FEATURE_READ)) {
        fprintf(stderr, "Error: %s\n", discid_get_error_msg(disc));
        discid_free(disc);
        return 1;
    }
    
    switch (output_mode) {
        case 1:  // ID only
            printf("%s\n", discid_get_id(disc));
            break;
            
        case 2:  // URL only
            printf("%s\n", discid_get_submission_url(disc));
            break;
            
        case 3:  // TOC info
            printf("%s %d %d", discid_get_id(disc), 
                   discid_get_first_track_num(disc),
                   discid_get_last_track_num(disc));
            for (int i = discid_get_first_track_num(disc); i <= discid_get_last_track_num(disc); i++) {
                printf(" %d", discid_get_track_offset(disc, i));
            }
            printf(" %d\n", discid_get_sectors(disc));
            break;
            
        case 4:  // All output
            printf("%s %d %d", discid_get_id(disc), 
                   discid_get_first_track_num(disc),
                   discid_get_last_track_num(disc));
            for (int i = discid_get_first_track_num(disc); i <= discid_get_last_track_num(disc); i++) {
                printf(" %d", discid_get_track_offset(disc, i));
            }
            printf(" %d\n", discid_get_sectors(disc));
            printf("%s\n", discid_get_submission_url(disc));
            break;
    }
    
    discid_free(disc);
    return 0;
}
