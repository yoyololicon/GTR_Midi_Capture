#include <stdio.h>
#include <iostream>
#include <vector>
#include <map>
#include <cmath>
#include <aubio/aubio.h>
#include <midifile/MidiFile.h>

/* set size of multi-channel frame-buffer */
#define NFRAMES (2048)
#define HOPSIZE (256)
#define DEFAULTSAMPLERATE (44100)

enum {ARG_PROGNAME, ARG_INFILE, ARG_OUTFILE, ARG_NARGS};

class pitch_buf
{
public:
    pitch_buf(){
        confidence =  6;
        range = 1.0;
        ref = 0;
    };
    ~pitch_buf(){};
    void add_smpl(smpl_t&);
    uchar midi_value();
    void clear();
private:
    vector<smpl_t> buf;
    uint_t confidence;
    smpl_t ref, range;
};

void pitch_buf::add_smpl(smpl_t &value)
{
    if(value)
    {
        if(fabs(value - ref) > range)
        {
            if(confidence)
                confidence--;
            else
            {
                //change the reference pitch value if the incoming value change dramatically
                ref = value;
                buf.clear();
                buf.push_back(ref);
                confidence = 20;
            }
        }
        else
        {
            buf.push_back(value);
            confidence = 20;
        }
    }
}

uchar pitch_buf::midi_value()
{
    //output the average value
    smpl_t sum = 0.0;
    for(vector<smpl_t>::iterator i = buf.begin(); i != buf.end(); i++)
        sum+=*i;
    return (uchar)(sum/buf.size());
}

void pitch_buf::clear()
{
    buf.clear();
    confidence = 6;
}

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
    aubio_onset_t *notes = NULL;
    aubio_tempo_t *tempo = NULL;
    aubio_pitch_t *pitch = NULL;

    pitch_buf ibuf;

/* STAGE 2 */
    printf("MAIN: generic process\n");

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
        printf("Error: unable to open infile %s\n",argv[ARG_INFILE]);
        return 1;
    }

    /* allocate space for  sample frame buffer ...*/
    n_frames_expected = aubio_source_get_duration(infile);
    samplerate = aubio_source_get_samplerate(infile);
    notes = new_aubio_onset("default", NFRAMES, HOPSIZE, samplerate);
    tempo = new_aubio_tempo("default", NFRAMES, HOPSIZE, samplerate);
    pitch = new_aubio_pitch("default", NFRAMES, HOPSIZE, samplerate);
    aubio_pitch_set_unit(pitch, "midi");

/* STAGE 4 */
    //create midi file object

    MidiFile outfile;
    outfile.absoluteTicks();
    outfile.addTrack(1);

    vector<uchar> midievent;
    midievent.resize(3);

    int tpq = 960;
    outfile.setTicksPerQuarterNote(tpq);

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

    framesread = 0;

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
    double p;
    vector<uint_t> grid;
    vector<double>::iterator itt = pos_0.begin();
    lastpos = p = *itt;
    itt++;
    for(; itt != pos_0.end(); itt++){
        do{
            p += time;
            count++;
            if(fabs(*itt-p) < 0.045){
                double l = (*itt - lastpos)/count;
                while(lastpos < *itt){
                    grid.push_back(lastpos*samplerate);
                    lastpos+=l;
                }
                p = *itt;
                lastpos = p;
                count = 0;
                break;
            }
        }while(p < *itt);
    }

    bpm = aubio_tempo_get_bpm(tempo);
    printf("average bpm %.3f\n", bpm);

    //set midi file tempo
    outfile.addTempo(0, 0, bpm);
    aubio_source_seek(infile, 0);
    // set the notes minion intervals base on bpm
    aubio_onset_set_minioi_s(notes, (smpl_t)10.0/bpm);

    double frame2tpq = bpm*tpq/(double)(samplerate*60);
    uint_t pos = 0;
    int pitchpos = 0;
    int haveNote = 0;
    vector<uint_t>::iterator g1 = grid.begin();
    vector<uint_t>::iterator g2 = g1+1;

    do{
        //process audio data to onset and pitch object
        aubio_source_do(infile, vec, &read);
        aubio_onset_do(notes, vec, tout);
        aubio_pitch_do(pitch, vec, pout);

        //check whether the notes have finished
        if(aubio_level_detection(vec, -50) == 1. && haveNote)
        {
            midievent[1] = ibuf.midi_value();
            outfile.addEvent(1, (int)(pitchpos*frame2tpq), midievent);

            midievent[0] = 0x80;
            outfile.addEvent(1, (int)(framesread*frame2tpq), midievent);
            haveNote = 0;
            ibuf.clear();
        }

        framesread += read;

        //check whether there is a new note
        if(tout->data[0]){
            pos = aubio_onset_get_last(notes);

            //set the note to grid
            if(g2 != grid.end()){
                while(*g2 <= pos){
                    g1 = g2;
                    g2++;
                    if(g2 == grid.end())
                        break;
                }

                if(*g1 <= pos && g2 != grid.end()){
                    if(pos - *g1 < (*g2 - *g1)/2)
                        pos = *g1;
                    else
                        pos = *g2;
                }
            }

            //kill the previous note
            if(haveNote)
            {
                midievent[1] = ibuf.midi_value();
                outfile.addEvent(1, (int)(pitchpos*frame2tpq), midievent);

                midievent[0] = 0x80;
                outfile.addEvent(1, (int)(pos*frame2tpq)-1, midievent);
                ibuf.clear();
            }
            //mark that there is a new note just happened
            midievent[0] = 0x90;
            midievent[2] = 127;
            haveNote = 1;
            pitchpos = pos;
        }
        else if(haveNote)
        {
            //add pitch value into buffer if the note haven't finished
            ibuf.add_smpl(pout->data[0]);
        }

    }while(read == HOPSIZE);

    //kill the last note
    if(haveNote)
    {
        midievent[0] = 0x80;
        outfile.addEvent(1, (int)(aubio_onset_get_last(notes)*frame2tpq), midievent);
    }

    //error checking
    if(framesread < 0)	{
        printf("Error reading infile.\n");
        return 1;
    }
    else
        printf("Done: read %d frames (expected %d) at %dHz (%d blocks) from %s\n",
               framesread, n_frames_expected, samplerate, framesread/HOPSIZE, argv[ARG_INFILE]);

    //write to real midi file
    outfile.sortTracks();
    outfile.write(argv[ARG_OUTFILE]);

    //close and release memory
    aubio_source_close(infile);

    del_fvec(vec);
    del_aubio_source(infile);
    del_aubio_onset(notes);

    return 0;
}