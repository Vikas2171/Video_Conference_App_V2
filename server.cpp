#include <gst/gst.h>
#include <iostream>
#include <glib.h>
#include <string.h>

using namespace std;

// SRTP Master Key: 32 bytes (AES-256) + 14 bytes (salt) = 46 bytes total
const char* SRTP_KEY = "0123456789012345678901234567890123456789012345"; // 46 bytes

// Create GstBuffer from key string
static GstBuffer* make_key_buffer(const char* key_str) {
    gsize key_len = strlen(key_str);
    GstBuffer *key_buf = gst_buffer_new_allocate(NULL, key_len, NULL);
    gst_buffer_fill(key_buf, 0, key_str, key_len);
    return key_buf;
}

// Signal handler for srtpdec request-key
static GstCaps* on_request_key(GstElement *srtpdec, guint ssrc, gpointer user_data) {
    cout << "Key requested for SSRC: " << ssrc << endl;
    
    GstBuffer *key_buf = make_key_buffer(SRTP_KEY);
    
    GstCaps *caps = gst_caps_new_simple("application/x-srtp",
        "srtp-key", GST_TYPE_BUFFER, key_buf,
        "srtp-cipher", G_TYPE_STRING, "aes-256-icm",
        "srtcp-cipher", G_TYPE_STRING, "aes-256-icm",
        "srtp-auth", G_TYPE_STRING, "hmac-sha1-80",
        "srtcp-auth", G_TYPE_STRING, "hmac-sha1-80",
        NULL);
    
    gst_buffer_unref(key_buf);
    
    return caps;
}

// Configure jitterbuffer for low latency
static void on_new_jitterbuffer(GstElement *rtpbin, GstElement *jitterbuffer, 
                                guint session, guint ssrc, gpointer user_data) {
    cout << "Configuring jitterbuffer for session " << session 
         << ", SSRC " << ssrc << endl;
    
    // Set low latency mode
    g_object_set(jitterbuffer,
        "latency", 50,                    // 50ms buffer (adjust based on network)
        "drop-on-latency", TRUE,          // Drop old packets
        "do-lost", FALSE,                 // Don't wait for lost packets
        "rtx-delay", 20,                  // Retransmission delay
        NULL);
}

// Bus message handler
static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    GMainLoop *loop = (GMainLoop *)data;
    
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            cout << "End of stream" << endl;
            g_main_loop_quit(loop);
            break;
            
        case GST_MESSAGE_ERROR: {
            gchar *debug;
            GError *error;
            gst_message_parse_error(msg, &error, &debug);
            
            cerr << "ERROR from element " << GST_OBJECT_NAME(msg->src) << ": " 
                 << error->message << endl;
            if (debug) {
                cerr << "Debugging info: " << debug << endl;
                g_free(debug);
            }
            g_error_free(error);
            g_main_loop_quit(loop);
            break;
        }
        
        case GST_MESSAGE_WARNING: {
            gchar *debug;
            GError *warning;
            gst_message_parse_warning(msg, &warning, &debug);
            
            cerr << "WARNING from element " << GST_OBJECT_NAME(msg->src) << ": " 
                 << warning->message << endl;
            if (debug) {
                cerr << "Debugging info: " << debug << endl;
                g_free(debug);
            }
            g_error_free(warning);
            break;
        }
        
        default:
            break;
    }
    
    return TRUE;
}

int main(int argc, char *argv[]) {
    gst_init(&argc, &argv);
    
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <client_ip>" << std::endl;
        return -1;
    }
    
    const char* client_ip = argv[1];
    
    cout << "Server starting with client IP: " << client_ip << endl;
    
    // Create pipeline with optimized settings
    std::string pipeline_desc =
        "rtpbin name=rtpbin_recv "
        "rtpbin name=rtpbin_send "
        
        // --- RECEIVE PATH (from client) - OPTIMIZED ---
        "udpsrc port=5000 buffer-size=212992 name=videortp_recv ! "
        "srtpdec name=videodec ! "
        "application/x-rtp, media=(string)video, clock-rate=(int)90000, "
        "encoding-name=(string)H264, payload=(int)96 ! rtpbin_recv.recv_rtp_sink_0 "
        "rtpbin_recv. ! rtph264depay ! avdec_h264 ! videoconvert ! autovideosink sync=false "
        "udpsrc port=5001 buffer-size=212992 ! srtpdec name=videortcpdec ! rtpbin_recv.recv_rtcp_sink_0 "
        
        "udpsrc port=5002 buffer-size=212992 name=audiortp_recv ! "
        "srtpdec name=audiodec ! "
        "application/x-rtp, media=(string)audio, clock-rate=(int)48000, "
        "encoding-name=(string)OPUS, payload=(int)97 ! rtpbin_recv.recv_rtp_sink_1 "
        "rtpbin_recv. ! rtpopusdepay ! opusdec ! audioconvert ! audioresample ! autoaudiosink sync=false "
        "udpsrc port=5003 buffer-size=212992 ! srtpdec name=audiortcpdec ! rtpbin_recv.recv_rtcp_sink_1 "
        
        // --- SEND PATH (to client) - OPTIMIZED ---
        "autovideosrc ! videoconvert ! video/x-raw,format=I420 ! "
        "x264enc tune=zerolatency bitrate=500 speed-preset=superfast "
        "key-int-max=30 bframes=0 aud=false byte-stream=true sliced-threads=true "
        "rc-lookahead=0 sync-lookahead=0 ! "
        "rtph264pay config-interval=1 pt=96 mtu=1400 ! rtpbin_send.send_rtp_sink_0 "
        "rtpbin_send.send_rtp_src_0 ! "
        "srtpenc name=videosendencrypt rtp-cipher=aes-256-icm rtcp-cipher=aes-256-icm rtp-auth=hmac-sha1-80 rtcp-auth=hmac-sha1-80 ! "
        "udpsink host=" + std::string(client_ip) + " port=5010 sync=false async=false "
        "rtpbin_send.send_rtcp_src_0 ! "
        "srtpenc name=videortcpenc rtp-cipher=aes-256-icm rtcp-cipher=aes-256-icm rtp-auth=hmac-sha1-80 rtcp-auth=hmac-sha1-80 ! "
        "udpsink host=" + std::string(client_ip) + " port=5011 sync=false async=false "
        "udpsrc port=5015 buffer-size=212992 ! srtpdec name=videortcprecvdec ! rtpbin_send.recv_rtcp_sink_0 "
        
        "autoaudiosrc ! audioconvert ! audioresample ! opusenc bitrate=64000 ! "
        "rtpopuspay pt=97 mtu=1400 ! rtpbin_send.send_rtp_sink_1 "
        "rtpbin_send.send_rtp_src_1 ! "
        "srtpenc name=audiosendencrypt rtp-cipher=aes-256-icm rtcp-cipher=aes-256-icm rtp-auth=hmac-sha1-80 rtcp-auth=hmac-sha1-80 ! "
        "udpsink host=" + std::string(client_ip) + " port=5012 sync=false async=false "
        "rtpbin_send.send_rtcp_src_1 ! "
        "srtpenc name=audiortcpenc rtp-cipher=aes-256-icm rtcp-cipher=aes-256-icm rtp-auth=hmac-sha1-80 rtcp-auth=hmac-sha1-80 ! "
        "udpsink host=" + std::string(client_ip) + " port=5013 sync=false async=false "
        "udpsrc port=5017 buffer-size=212992 ! srtpdec name=audiortcprecvdec ! rtpbin_send.recv_rtcp_sink_1";
    
    GError *error = nullptr;
    GstElement *pipeline = gst_parse_launch(pipeline_desc.c_str(), &error);
    
    if (!pipeline) {
        std::cerr << "Failed to create pipeline: " << error->message << std::endl;
        g_clear_error(&error);
        return -1;
    }
    
    // Configure rtpbin for low latency
    GstElement *rtpbin_recv = gst_bin_get_by_name(GST_BIN(pipeline), "rtpbin_recv");
    GstElement *rtpbin_send = gst_bin_get_by_name(GST_BIN(pipeline), "rtpbin_send");
    
    if (rtpbin_recv) {
        g_object_set(rtpbin_recv, 
            "latency", 50,                    // 50ms latency (adjust for network)
            "drop-on-latency", TRUE,          // Drop packets that arrive too late
            "do-retransmission", FALSE,       // Disable retransmission for lower latency
            NULL);
        
        // Connect jitterbuffer configuration signal
        g_signal_connect(rtpbin_recv, "new-jitterbuffer", G_CALLBACK(on_new_jitterbuffer), NULL);
    }
    
    if (rtpbin_send) {
        g_object_set(rtpbin_send,
            "latency", 50,
            "drop-on-latency", TRUE,
            "do-retransmission", FALSE,
            NULL);
        
        g_signal_connect(rtpbin_send, "new-jitterbuffer", G_CALLBACK(on_new_jitterbuffer), NULL);
    }
    
    // Set keys for srtpenc elements
    GstBuffer *key_buffer = make_key_buffer(SRTP_KEY);
    
    GstElement *videosendencrypt = gst_bin_get_by_name(GST_BIN(pipeline), "videosendencrypt");
    GstElement *videortcpenc = gst_bin_get_by_name(GST_BIN(pipeline), "videortcpenc");
    GstElement *audiosendencrypt = gst_bin_get_by_name(GST_BIN(pipeline), "audiosendencrypt");
    GstElement *audiortcpenc = gst_bin_get_by_name(GST_BIN(pipeline), "audiortcpenc");
    
    if (videosendencrypt) g_object_set(videosendencrypt, "key", key_buffer, NULL);
    if (videortcpenc) g_object_set(videortcpenc, "key", key_buffer, NULL);
    if (audiosendencrypt) g_object_set(audiosendencrypt, "key", key_buffer, NULL);
    if (audiortcpenc) g_object_set(audiortcpenc, "key", key_buffer, NULL);
    
    if (videosendencrypt) gst_object_unref(videosendencrypt);
    if (videortcpenc) gst_object_unref(videortcpenc);
    if (audiosendencrypt) gst_object_unref(audiosendencrypt);
    if (audiortcpenc) gst_object_unref(audiortcpenc);
    gst_buffer_unref(key_buffer);
    
    // Connect request-key signals for srtpdec elements
    GstElement *videodec = gst_bin_get_by_name(GST_BIN(pipeline), "videodec");
    GstElement *videortcpdec = gst_bin_get_by_name(GST_BIN(pipeline), "videortcpdec");
    GstElement *audiodec = gst_bin_get_by_name(GST_BIN(pipeline), "audiodec");
    GstElement *audiortcpdec = gst_bin_get_by_name(GST_BIN(pipeline), "audiortcpdec");
    GstElement *videortcprecvdec = gst_bin_get_by_name(GST_BIN(pipeline), "videortcprecvdec");
    GstElement *audiortcprecvdec = gst_bin_get_by_name(GST_BIN(pipeline), "audiortcprecvdec");
    
    if (videodec) g_signal_connect(videodec, "request-key", G_CALLBACK(on_request_key), NULL);
    if (videortcpdec) g_signal_connect(videortcpdec, "request-key", G_CALLBACK(on_request_key), NULL);
    if (audiodec) g_signal_connect(audiodec, "request-key", G_CALLBACK(on_request_key), NULL);
    if (audiortcpdec) g_signal_connect(audiortcpdec, "request-key", G_CALLBACK(on_request_key), NULL);
    if (videortcprecvdec) g_signal_connect(videortcprecvdec, "request-key", G_CALLBACK(on_request_key), NULL);
    if (audiortcprecvdec) g_signal_connect(audiortcprecvdec, "request-key", G_CALLBACK(on_request_key), NULL);
    
    if (videodec) gst_object_unref(videodec);
    if (videortcpdec) gst_object_unref(videortcpdec);
    if (audiodec) gst_object_unref(audiodec);
    if (audiortcpdec) gst_object_unref(audiortcpdec);
    if (videortcprecvdec) gst_object_unref(videortcprecvdec);
    if (audiortcprecvdec) gst_object_unref(audiortcprecvdec);
    
    if (rtpbin_recv) gst_object_unref(rtpbin_recv);
    if (rtpbin_send) gst_object_unref(rtpbin_send);
    
    // Set up bus watch
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
    guint bus_watch_id = gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);
    
    cout << "Setting pipeline to PLAYING..." << endl;
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    
    if (ret == GST_STATE_CHANGE_FAILURE) {
        cerr << "Unable to set pipeline to PLAYING!" << endl;
        gst_object_unref(pipeline);
        return -1;
    }
    
    cout << "\n=== LOW LATENCY Server with SRTP AES-256 running ===" << endl;
    cout << "Configured with 50ms jitter buffer latency" << endl;
    cout << "Press Ctrl+C to quit.\n" << endl;
    
    g_main_loop_run(loop);
    
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_source_remove(bus_watch_id);
    g_main_loop_unref(loop);
    
    return 0;
}

