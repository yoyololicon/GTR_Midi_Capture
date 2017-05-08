#include <stdio.h>
#include <iostream>
#include <fstream>
#include <map>
#include <vector>
#include <aubio/aubio.h>
using namespace std;

/* set size of multi-channel frame-buffer */
#define NFRAMES (2048)
#define HOPSIZE (256)
#define DEFAULTSAMPLERATE (44100)

enum {ARG_PROGNAME, ARG_INFILE, ARG_OUTFILE, ARG_NARGS};

int main(int argc, char* argv[])
{
/* STAGE 1 */
    uint_t framesread = 0;
    uint_t n_frames_expected = 0;
    uint_t samplerate = DEFAULTSAMPLERATE;
    fvec_t *vec = new_fvec(HOPSIZE);
    fvec_t *tout = new_fvec(1);
    fvec_t *pout = new_fvec(1);
    /* init all dynamic resources to default states */
    aubio_source_t *infile = NULL;
    aubio_tempo_t *tempo = NULL;
    aubio_pitch_t *pitch = NULL;

/* STAGE 2 */
    cout << "MAIN: generic process" << endl;

    /* process any optional flags: remove this block if none used! */
    if(argc > 1){
        char flag;
        while(argv[1][0] == '-'){
            flag = argv[1][1];
            switch(flag){
                case('\0'):
                    printf("Error: missing flag name\n");
                    return 1;
                default:
                    break;
            }
            argc--;
            argv++;
        }
    }

    /* check rest of commandline */
    if(argc < ARG_NARGS){
        printf("insufficient arguments.\n"
                       "usage: MAIN infile outfile\n"
        );
        return 1;
    }

/* STAGE 3 */
    infile = new_aubio_source(argv[ARG_INFILE], 0, HOPSIZE);
    if(!infile){
        cout << "Error: unable to open infile " << string(argv[ARG_INFILE]) << endl;
        return 1;
    }

    /* allocate space for  sample frame buffer ...*/
    n_frames_expected = aubio_source_get_duration(infile);
    samplerate = aubio_source_get_samplerate(infile);
    tempo = new_aubio_tempo("default", NFRAMES, HOPSIZE, samplerate);
    pitch = new_aubio_pitch("default", NFRAMES, HOPSIZE, samplerate);
    aubio_pitch_set_unit(pitch, "midi");

/* STAGE 4 */
    fstream fout;
    fout.open(argv[ARG_OUTFILE], fstream::out);

/* STAGE 5 */
    printf("processing....\n");

    uint_t read = 0;
    smpl_t bpm = 0;

    double lastpos = 0;
    map<int, int> interval;
    map<int, int>::iterator it;
    vector<double> pos_0;

    do{
        //process audio data to onset and pitch object
        aubio_source_do(infile, vec, &read);
        aubio_pitch_do(pitch, vec, pout);
        aubio_tempo_do(tempo, vec, tout);

        framesread += read;
        if(pout->data[0] == 0 && aubio_level_detection(vec, -50) < 0){
            double pos = framesread/(double)samplerate;
            pos_0.push_back(pos);
            if(lastpos > 0){
                lastpos = pos - lastpos;
                int temp = lastpos*1000;
                if(temp < 10)
                    continue;
                it = interval.find(temp);
                if(it != interval.end())
                    it->second++;
                else
                    interval[temp] = 1;
            }
            lastpos = pos;
        }

    }while(read == HOPSIZE);

    int tmp = 0;
    double time;
    for(it = interval.begin(); it != interval.end(); it++){
        if(tmp < it->second){
            time = (double)(it->first);
            tmp = it->second;
        }
    }

    time /=1000;
    cout << "the smallest interval is " << time << " s" << endl;

    //compute the grid
    int count = 0;
    double pos;
    lastpos = 0.0;
    for(vector<double>::iterator itt = pos_0.begin(); itt != pos_0.end(); itt++){
        if(lastpos > 0.0){
            while(pos < *itt){
                pos += time;
                count++;
            }
            double pos2 = pos - time;
            int count2 = count - 1;
            if(pos - *itt < 0.02){
                double l = (*itt - lastpos)/count;
                while(lastpos < *itt){
                    cout << lastpos << endl;
                    lastpos+=l;
                }
                pos = *itt;
                lastpos = pos;
                time =  (time + l)/2;
                count = 0;
            }
            else if(*itt - pos2 < 0.02 && count2 > 0){
                double l = (*itt - lastpos)/count2;
                while(lastpos < *itt){
                    cout << lastpos << endl;
                    lastpos+=l;
                }
                pos = *itt;
                lastpos = pos;
                time =  (time + l)/2;
                count = 0;
            }
        }
        else {
            pos = *itt;
            lastpos = pos;
        }
    }

    for(it = interval.begin(); it != interval.end(); it++){
        fout << it->first << "\t" << it->second << endl;
    }

    //error checking
    if(framesread < 0)	{
        cout << "Error reading infile." << endl;
        return 1;
    }
    else
        cout << "Done: read " << framesread
             << " frames (expected " << n_frames_expected
             << ") at " << samplerate
             << "Hz (" << framesread/HOPSIZE
             << " blocks) from " << argv[ARG_INFILE] << endl;

    bpm = aubio_tempo_get_bpm(tempo);
    cout << "average bpm " << bpm << endl;
    //close and release memory
    aubio_source_close(infile);

    fout.close();
    del_fvec(vec);
    del_aubio_source(infile);

    return 0;
}
