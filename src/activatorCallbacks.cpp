#include "activatorCallbacks.h"
#include "activatorWindow.h"
#include "gfcStructures.h"
#include "UIConstants.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <iostream>
#include <cstdlib>
#include <fstream>
#include <sstream> //for stingstream

#include <botan/dsa.h>
#include <botan/rsa.h>
#include <botan/md5.h>
#include <botan/botan.h>
#include <botan/look_pk.h>
#include <botan/lookup.h>
#include <botan/botan.h>
#include <botan/x509cert.h>
#include <botan/oids.h>

#include <time.h>
#include <FL/fl_ask.H>
#include <FL/Fl_File_Chooser.H>

#ifdef WIN32
#include <Iphlpapi.h> //for macAddress in windows
#include <assert.h>
//#include <Winsock2.h> //for hostname in windows
#endif

//required to send the email, part of raknet
#include "EmailSender.h"
#include "FileList.h"

#include <curl/curl.h>

extern ActivatorWindow actW;
#define SERIAL_LEN 30

#include "JefeCorp_JefeCheck1_ActivatorRSA_public.h"



std::string encryptActivationMessage(std::string message, std::string passprhase) {
    using namespace Botan;
    using namespace std;
    std::string result;

    std::string keyString=passprhase;
    int padding=56-keyString.length();
    for (int i=0; i<padding;i++) {
        keyString+="0";
    }
    keyString=hex_encode(keyString);

    SymmetricKey skey=OctetString(keyString);
    InitializationVector iv=OctetString("3132333435363738");
    Pipe pipe(get_cipher("Blowfish/CBC", skey,iv, ENCRYPTION),new Hex_Encoder());
    pipe.process_msg(message);
    result=pipe.read_all_as_string(0);
    return result;
}

int saveActivation(std::string filename, std::string message) {

    //save to a file
    Fl_File_Chooser *fc=new  Fl_File_Chooser ( ".",NULL,0,"Choose a directory to store the activation file" );
    fc->preview ( 0 );
    fc->filter ( NULL );
    fc->type ( Fl_File_Chooser::DIRECTORY );
    fc->show();
    while ( fc->visible() )
        Fl::wait();

    if ( fc->count() ) {
        boost::filesystem::path outPath(fc->value ( 0 ));
        outPath=outPath/filename;
        std::ofstream outFile(outPath.string().c_str());
        if (outFile) {

            outFile<<message;
            outFile.close();

            fl_cursor(FL_CURSOR_DEFAULT);
            Fl::check();
            std::string confirmationText="An activation file has been saved to\n\n";
            confirmationText+=outPath.string();
            confirmationText+="\n\nPlease add it as an attachment in an email to activator@jefecorp.com\nIf your Purchase Confirmation Number is valid, the license for this computer will be delivered to the email registered during purchase in no more than one workday";
            actW.successMessageSave->buffer()->text(confirmationText.c_str());
            actW.wizard->value(actW.pageSuccessSave);

        } else {
            fl_alert("Error when generating activation file, please try again");
            actW.wizard->value(actW.pageFailure);
            return 0;

        }
    } else {
        return 1;
    }

}

enum postErrors {GFC_LICENSEREQUEST_POST_OK,GFC_LICENSEREQUEST_POST_FAILURE,GFC_LICENSEREQUEST_POST_SERVER_RESPONSE_FAILURE};

/****EXTRA FUNCTIONS AND STRUCTURES TO PARSE THE CURL RESPONSE****/
struct MemoryStruct {
    char *memory;
    size_t size;
};

static void *myrealloc(void *ptr, size_t size);

static void *myrealloc(void *ptr, size_t size) {
    /* There might be a realloc() out there that doesn't like reallocing
    NULL pointers, so we take care of it here */
    if (ptr)
        return realloc(ptr, size);
    else
        return malloc(size);
}

static size_t
WriteMemoryCallback(void *ptr, size_t size, size_t nmemb, void *data) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)data;

    mem->memory = (char*)myrealloc(mem->memory, mem->size + realsize + 1);
    if (mem->memory) {
        memcpy(&(mem->memory[mem->size]), ptr, realsize);
        mem->size += realsize;
        mem->memory[mem->size] = 0;
    }
    return realsize;
}



int postActivation(std::string message, std::string passprhase, std::string &errorString) {

    using namespace std;
    int result;
    //POST THE MESSAGE TO THE CGI PERL SCRIPT USING CURL's HTTPS
    curl_global_init(CURL_GLOBAL_ALL);

    CURL *curl;
    CURLcode res;

    struct curl_httppost *formpost=NULL;
    struct curl_httppost *lastptr=NULL;
    struct curl_slist *headerlist=NULL;
    static const char buf[] = "Expect:";

    ofstream debugFile("/tmp/debugFile.txt",ofstream::out);
    debugFile << message <<std::endl;
    debugFile << passprhase;
    debugFile.close();

    /* Fill in the text field */
    curl_formadd(&formpost,
                 &lastptr,
                 CURLFORM_COPYNAME, "message",
                 CURLFORM_COPYCONTENTS, message.c_str(),
                 CURLFORM_END);


    /* Fill in the submit field too, even if this is rarely needed */
    curl_formadd(&formpost,
                 &lastptr,
                 CURLFORM_COPYNAME, "passphrase",
                 CURLFORM_COPYCONTENTS, passprhase.c_str(),
                 CURLFORM_END);

    curl = curl_easy_init();
    /* initalize custom header list (stating that Expect: 100-continue is not
       wanted */
    headerlist = curl_slist_append(headerlist, buf);
	//printf("sending passphrase %s\n",passprhase.c_str());
	//printf("sending message %s\n",message.c_str());
	
    if (curl) { 
        /* what URL that receives this POST */
        curl_easy_setopt(curl, CURLOPT_URL, "https://secure24.inmotionhosting.com/~jefeco5/jefecheck/cgi-bin/lg.cgi");
		//curl_easy_setopt(curl, CURLOPT_URL, "http://www.jefecorp.com/jefecheck/cgi-bin/lg.cgi");
		//curl_easy_setopt(curl, CURLOPT_URL, "https://secure24.inmotionhosting.com/~jefeco5/perltest/FormHello.cgi");

        //setup the write callback so we can store the response from the server and parse it
        struct MemoryStruct chunk; 

        chunk.memory=NULL; /* we expect realloc(NULL, size) to work */
        chunk.size = 0;    /* no data at this point */

        curl_global_init(CURL_GLOBAL_ALL);
        /* send all data to this function  */
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

        /* we pass our 'chunk' struct to the callback function */
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

        /* some servers don't like requests that are made without a user-agent
        field, so we provide one */
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
		
		/* set the timeout */
		curl_easy_setopt(curl,CURLOPT_TIMEOUT,5);

        //we are using the shared server certificate, so disable checking the domain
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);

        curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
		
        res = curl_easy_perform(curl);

        /* always cleanup */
        curl_easy_cleanup(curl);
        /* then cleanup the formpost chain */
        curl_formfree(formpost);
        /* free slist */
        curl_slist_free_all (headerlist);

        if (res!=CURLE_OK) {
            printf("cURL error: %i\n",res);
            errorString = curl_easy_strerror(res);
            result = GFC_LICENSEREQUEST_POST_FAILURE;

        } else {

			//printf("///*****START SERVER REPLY******/\n%s\n///*******END SERVER REPLY*******////",chunk.memory);

            //parse the post result, see what it says and return the error
			if (strstr(chunk.memory,"Error:")>0) {
				//got an error...
				printf("did not find 'Request received correctly' in chunk\n");
				errorString=chunk.memory;
				result= GFC_LICENSEREQUEST_POST_SERVER_RESPONSE_FAILURE;
                
            } else {
				printf("found 'Request received correctly' in chunk\n");
				result= GFC_LICENSEREQUEST_POST_OK;
				errorString=chunk.memory;
            }
        }
        if (chunk.memory)
            free(chunk.memory);

    }

    //return the result
    return result;

}


void removeWhiteSpaces ( std::string &data ) {
    while ( data.find ( " " ) !=std::string::npos ) {
        data.erase ( data.find ( " " ),1 );
    }
}

std::string createActivationMessage( std::string requesterName, std::string clientName, std::string companyName, std::string email, std::string osName, std::string paymentConfirmation, std::string passphrase) {
    using namespace std;
    std::string clearText;
    clearText="";
    clearText+=std::string("requestername=")+string ( requesterName ) +string ("\n");
    clearText+=std::string("clientname=")+string ( clientName ) +string ("\n");
    clearText+=std::string("companyname=")+string ( companyName ) +string ("\n");
    clearText+=std::string("os=")+string (osName) +string ("\n");
    clearText+=std::string("purchaseconfirmationnumber=")+string ( paymentConfirmation ) +string ("\n");
    clearText+=std::string("email=")+string ( email) +string ("\n");
    clearText+=std::string("jefecheckversion=")+std::string(JEFE_VERSION)+string ("\n");
    //clearText+=std::string("jefecheckversion=")+std::string("3.8.255")+string ("\n");
    clearText+=std::string("mac=")+getMACAddress()[0]+string ("\n");

    //cout << "ClearText:\n" <<clearText <<std::endl;

    return encryptActivationMessage(clearText,passphrase);
}

void createActivationCB ( Fl_Widget* w, void* p ) {

    switch ((long)p) {
    case ACTIVATORDONEBUTTON_ID:
        actW.activatorWindow->hide();
        break;
    
    case ACTIVATORTRYAGAINBUTTON_ID:
    	actW.wizard->value(actW.pageStart);
    	break;
    	
    case ACTIVATORBUYBUTTON_ID:
    	printf("redirecting to purchase page\n");
    	 fl_open_uri("http://jefecheck.jefecorp.com/purchase.html");
    	break;
    
    case ACTIVATORSAVEBUTTON_ID:
    case ACTIVATORCREATEBUTTON_ID:
        using namespace std;

	std::string requesterName=actW.requesterName->value();
        std::string clientName=actW.clientName->value();
        std::string companyName=actW.companyName->value();
        std::string email=actW.email->value();
        std::string osName=getOS();
        std::string purchaseConfirmationNumber=actW.purchaseConfirmationNumber->value();
	
        if ( requesterName.length() ==0  ) {
            fl_alert ( "Please specify your name\n" );
            actW.requesterName->take_focus();
            return;
        }
        
        
        
        if ( clientName.length() ==0  ) {
            fl_alert ( "Please specify a computer name\n" );
             actW.clientName->take_focus();
            return;
        }
        
        if ( companyName.length() ==0  ) {
            fl_alert ( "Please specify a company name\n" );
            actW.companyName->take_focus();
            return;
        }
        
        if (!checkValidEmail(email)) {
            fl_alert ( "Please use a valid email\n" );
            actW.email->take_focus();
            return;
        }
	
        if ( purchaseConfirmationNumber.length()!=14) {
            fl_alert ( "The Purchase Confirmation Number does not seem valid\n" );
            actW.purchaseConfirmationNumber->take_focus();
            return;
        }

        //actW.controlsGroup->deactivate();
        //actW.sendButton->copy_label("Sending Request...");
       

        if ((long)p==ACTIVATORCREATEBUTTON_ID) {
            
             	actW.waitingMessage->buffer()->text("Please Wait... your request is being sent...");
        	actW.wizard->value(actW.pageWaiting);
        
        	fl_cursor(FL_CURSOR_WAIT);
        	Fl::check();
            
            std::string theMessage=createActivationMessage(requesterName, clientName,companyName, email,osName,purchaseConfirmationNumber,purchaseConfirmationNumber);
            //post the activation
            bool tryAgain=false;
            
            std::string errorString;
            int postResult=postActivation(theMessage,purchaseConfirmationNumber,errorString);

            if (postResult==GFC_LICENSEREQUEST_POST_OK) {
                    //SUCCESS!!
                    //set the success message and disable the GUI
                    printf("Success\n");

			actW.successMessage->buffer()->text("Success!\nYour request has been sent.\n\n If your Purchase Confirmation Number is valid, the license for this computer will be delivered to the email registered during purchase in a few minutes");
			actW.wizard->value(actW.pageSuccess);
			
                    fl_cursor(FL_CURSOR_DEFAULT);
                    Fl::check();
                } else {
                    int selectedOption=0;
                    std::string alert= "License Request Failed\n";
					alert+="\nError: "+errorString.substr(errorString.find("Error:"),std::string::npos)+"\n";
                    alert+="\nLicense request requires a working internet connection\n";
                    alert+="\nYou can try to send the request again\n\nor\n\nsave the activation file and email to JefeCorp yourself";
                    
                    actW.failureMessage->buffer()->text(alert.c_str());
		    actW.wizard->value(actW.pageFailure);

                }
        } else {
            //Just save the activation
			fl_alert("Please select a folder to save the activation file");
            std::string theMessage=createActivationMessage(requesterName, clientName,companyName, email,osName,purchaseConfirmationNumber,"12345678901234");
            removeWhiteSpaces(clientName);
            std::string theFilename=clientName+std::string("_JefecheckActivation.act");
            saveActivation(theFilename, theMessage);
            
        }

        break;
    }

}

void fillActivationWindowDefaults() {
    static Fl_Text_Buffer actWbuffer;
    static Fl_Text_Buffer actWbufferSuccess;
    static Fl_Text_Buffer actWbufferFailure;
    static Fl_Text_Buffer actWbufferSuccessSave;
    static Fl_Text_Buffer actWbufferWaiting;
    actWbuffer.text("Please enter the required information to request a JefeCheck License for this computer.\nRemember you must purchase one or more licenses before you use this form.");
	
    actW.message->textsize(12);
    actW.message->wrap_mode(1,70);
    actW.message->buffer(actWbuffer);
    
    
    actW.waitingMessage->buffer(actWbufferWaiting);
    actW.waitingMessage->textsize(12);
    actW.waitingMessage->wrap_mode(1,70);
    
    actW.failureMessage->buffer(actWbufferFailure);
    actW.failureMessage->textsize(12);
    actW.failureMessage->wrap_mode(1,70);
    
    actW.successMessage->buffer(actWbufferSuccess);
    actW.successMessage->textsize(12);
    actW.successMessage->wrap_mode(1,70);
    
    actW.successMessageSave->buffer(actWbufferSuccessSave);
    actW.successMessageSave->textsize(12);
    actW.successMessageSave->wrap_mode(1,70);
    
    actW.wizard->value(actW.pageStart);
    
    //Client Name
    if (std::string(actW.clientName->value())=="") {
        actW.clientName->value(getHostname().c_str());
    }

    //OS
#ifdef __APPLE__
    actW.osName->value(1);
#endif

#ifdef WIN32
    actW.osName->value(0);
#endif

#ifdef linux
    actW.osName->value(2);
#endif

}
