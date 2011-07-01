
#include <FL/Fl.h>
#include <FL/Fl_Button.h>

#include <string>

void createActivationCB(Fl_Widget*, void*);
void createActivation(std::string clientName, std::string companyName, std::string email, std::string osName, std::string paymentMethod, std::string paymentConfirmation,  std::string outPath );
void fillActivationWindowDefaults();