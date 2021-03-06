/* Camera System Control 
 *  
 *Parsing Code Credit to: 
 *Robin2 - "Serial Input Basics updated" available: http://forum.arduino.cc/index.php?topic=396450
 */
#include <Servo.h>
#include <PID_v1.h>


//Constants
  const float degToRad = 3.14159265359/180;
  const float radToDeg = 1/degToRad;
  const int calPin1 = 4;
  const int calPin2 = 5;
  const int calPin3 = 6;
  const int calPin4 = 7;

  const int panFeedBackPin = 2;
  const int panPin = 8;

  const int tiltPin = 9;
  const int tiltOffset = 70;
  const int zoomPin = 10;

//holds calibration/initialization data
  float lat0=0.0;
  float lon0=0.0;
  float ele0=0.0;
  double lonCorrection=0.0;
  
//holds GPS transmistter data
  float dLat=0.0,dLatOld=0.0;
  float dLon=0.0, dLonOld=0.0;
  float dEle=0.0, dEleOld=0.0;
  float distance=0.0,distanceOld=0.0;
  
//holds Pan angle data
  const int panWeight = 80;
  int newQuadrant = 1, oldQuadrant = 1;
  long panAngle=0, panAngleOld=0;
  double doublePanAngle=0;
  unsigned long durationLow = 0, durationHigh = 0;
  long rotation = 0;
  unsigned int dutyCycle = 0, theta = 0;
  
//Pan Control data
  const double outputMax = 1620;
  const double outputMin = 1360;
  const int timeStep=100;   
  double targetPanAngle=105;
  double outputPanServo=1500;
  double Kp=16;
  double Ki=1.5;
  double Kd=.8;

//Time
  unsigned long millisStart,panMillis,panServoMillis,targetAngleINCMillis;  

//Serial parsing
  const byte stringSize = 64;
  char receivedChars[stringSize];
  char tempChars[stringSize];
  float gpsLat=0.0; 
  float gpsLon=0.0;
  float gpsEle=0.0;
  float gpsEleOld=0.0;
  float gpsVoltage=0.0;
  boolean newData = false;
  
//Instatiate Objects  
Servo panServo;
Servo tiltServo;
Servo zoomServo;

//Signal Processing Variables
const int WinSize = 10;
int winIdx = 0;
float winLat[WinSize];
float winLon[WinSize];

//Instantiate PID Object
PID panPID(&doublePanAngle, &outputPanServo, &targetPanAngle, Kp, Ki, Kd, REVERSE,P_ON_M); 

void recvSerialData(Stream &ser) {
    static boolean recvInProgress = false;
    static byte ndx = 0;
    char startMarker = '<';
    char endMarker = '>';
    char rc;

    while (ser.available() > 0 && newData == false) {
        rc = ser.read();

        if (recvInProgress == true) {
            if (rc != endMarker) {
                receivedChars[ndx] = rc;
                ndx++;
                if (ndx >= stringSize) {
                    ndx = stringSize - 1;
                }
            }
            else {
                receivedChars[ndx] = '\0'; // terminate the string
                recvInProgress = false;
                ndx = 0;
                newData = true;
            }
        }

        else if (rc == startMarker) {
            recvInProgress = true;
        }
    }
}

void parseGPSData() {      // split the data into its parts and feed signal processing buffer  
    char * strtokIndx;  // this is used by strtok() as an index
    strcpy(tempChars, receivedChars); //make a copy of the received string
    
    strtokIndx = strtok(tempChars,","); // get the first part - the string
    gpsLat = atof(strtokIndx); // convert this part to a float
  
    strtokIndx = strtok(NULL, ","); // this continues where the previous call left off
    gpsLon = atof(strtokIndx);     // convert this part to a float
 
    strtokIndx = strtok(NULL, ",");
    gpsEle = atof(strtokIndx);     // convert this part to a float

    strtokIndx = strtok(NULL, ",");
    gpsVoltage = atof(strtokIndx);     // convert this part to a float
}

void getPanAngle(){   
  durationLow = pulseIn(panFeedBackPin, LOW); //Measures the time the feedback signal is low
  durationHigh = pulseIn(panFeedBackPin, HIGH); //Measures the time the feedback signal is high
  dutyCycle = (10000*durationHigh)/(durationHigh+durationLow); //calculates the duty cycle
 
  //Formula for angle calculation taken from parallax 360 feedback data sheet
  theta = (35900*(dutyCycle - 290))/(9710-290+1)/100;  
  if(theta > 359) theta = 359; //limits theta to bounds

  //Determines what quadrant the servo rotates from to determine positive and negative angles outside 0-359
  if( theta>=0 && theta<=90){
    oldQuadrant = newQuadrant; 
    newQuadrant = 1;
  }
  if( theta>90 && theta<=180){
    oldQuadrant = newQuadrant; 
    newQuadrant = 2;
  }
    if( theta>180 && theta<=270){
    oldQuadrant = newQuadrant; 
    newQuadrant = 3;
  }
  else if( theta>270 && theta<=359 ){
   oldQuadrant = newQuadrant; 
   newQuadrant = 4;
  }
  if(oldQuadrant==1 && newQuadrant==4){ 
    rotation = rotation - 1; 
  }
  else if( oldQuadrant==4 && newQuadrant==1){
    rotation = rotation + 1;
  }
  panAngleOld = panAngle;   
  panAngle = (panWeight*panAngleOld + (100-panWeight)*(360*rotation + theta))/100;
  doublePanAngle = (double)panAngle; //cast the pan angle to double for pid
 /*
 //Print the Data 
  Serial1.print("newQuadrant: ");  Serial1.print(newQuadrant);  Serial1.print("\t");
  Serial1.print("oldQuadrant: ");  Serial1.print(oldQuadrant);  Serial1.print("\t");
  Serial1.print("Theta: ");  Serial1.print(theta,DEC);  Serial1.print("\t");
  Serial1.print("Rotation #: ");  Serial1.print(rotation);  Serial1.print("\t");
  Serial1.print("panAngle: ");  Serial1.println(panAngle,DEC);
 */ 
}

void updateTranLoc(){
  dLatOld = dLat;
  dLonOld = dLon;
  dEleOld = dEle;
  distanceOld = distance;
  dLat = gpsLat - lat0;
  dLon = gpsLon - lon0;
  dLon = lonCorrection*dLon;
  dEle= gpsEle-ele0;
  distance = sqrt(dLat*dLat +dLon*dLon)*111009; //gives distance in meters
}

//Calibrate camera system
void calibration(){   
  tiltServo.writeMicroseconds(1500+tiltOffset);
    
   //SET THE LONGITUDE,LATITUDE, AND ELEVATION OF THE CAMERA SYSTEM
   if(digitalRead(calPin1)== HIGH && digitalRead(calPin2) == HIGH){   
    lat0 = gpsLat;
    lon0 = gpsLon;
    ele0 = gpsEle;
    lonCorrection = cos(degToRad*lat0); //correction for longitude values using loca, flat earth approx.
   
    Serial.print(millis()-millisStart); Serial.print("\t");
    Serial.print("lat0: ");  Serial.print(lat0,9);  Serial.print("\t");
    Serial.print("lon0: ");  Serial.print(lon0,9);  Serial.print("\t");
    Serial.print("ele0: ");  Serial.println(ele0,3);
   }
   //SET TRIPOD TO ALIGN WITH GPS TRANSMITTER AND CALCULATE INITIAL POSITION VECTOR
   //When aligning camera lens ensure GPS transmistter is centered about Y axis
   else if(digitalRead(calPin1)== HIGH && digitalRead(calPin2) == LOW){
    updateTranLoc();
    Serial.print(millis()-millisStart); Serial.print("\t");
    Serial.print("dlat: ");  Serial.print(dLat,9);  Serial.print("\t");
    Serial.print("dlon: ");  Serial.print(dLon,9);  Serial.print("\t");
    Serial.print("distance: ");  Serial.print(distance,6);  Serial.print("\t");
    Serial.print("dele: ");  Serial.println(dEle,3);  
   } 
}



void winLatAndLon(){
  winLat[winIdx]=gpsLat;
  winLon[winIdx]=gpsLon;
  winIdx++;
  if(winIdx >= WinSize)  winIdx = 0;
}
  
void updateTargetPanAngle(){ //updates target pan angle with a regression line connecting to the camera system through 10 gps readings 
  float regL=0;
  float top=0;
  float bot=0;
    
  for(int i=0; i<WinSize; i++){
    top += (winLat[i]-lat0)*(winLon[i]-lon0);
    bot += winLat[i]*winLat[i];
  }
  regL = top/bot;
  if(regL>0 && dLon<0) targetPanAngle = doublePanAngle + radToDeg*arcTan(regL) - 180;  
  else if(regL<0 && dLon>0)  targetPanAngle = doublePanAngle + radToDeg*arcTan(regL) + 180; 
  else targetPanAngle = doublePanAngle + radToDeg*arcTan(regL);
  
  Serial.print("regline slope: "); Serial.print(regL); Serial.print("  new targetPanAngle: "); Serial.print(targetPanAngle);
}

void maintenanceMode(){
  panServo.writeMicroseconds(1500);
  tiltServo.write(175);  
}

//Series Solution for the trig solutions below by Abhilash Patel. Available: http://www.instructables.com/id/Arduino-Trigonometric-Inverse-Functions/
float arcSin(float c){
  float out;
  out= ((c+(c*c*c)/6+(3*c*c*c*c*c)/40+(5*c*c*c*c*c*c*c)/112+
  (35*c*c*c*c*c*c*c*c*c)/1152 +(c*c*c*c*c*c*c*c*c*c*c*0.022)+
  (c*c*c*c*c*c*c*c*c*c*c*c*c*.0173)+(c*c*c*c*c*c*c*c*c*c*c*c*c*c*c*.0139)+
  (c*c*c*c*c*c*c*c*c*c*c*c*c*c*c*c*c*0.0115)+(c*c*c*c*c*c*c*c*c*c*c*c*c*c*c*c*c*c*c*0.01)
   ));
                                           //asin
  if(c>=.96 && c<.97){out=1.287+(3.82*(c-.96)); }
  if(c>=.97 && c<.98){out=(1.325+4.5*(c-.97));}          // arcsin
  if(c>=.98 && c<.99){out=(1.37+6*(c-.98));}
  if(c>=.99 && c<=1){out=(1.43+14*(c-.99));}  
  return out;// in radians
}

float arcCos(float c){
  float out;
  out=arcSin(sqrt(1-c*c));
  return out; // in radians
}

float arcTan(float c){
  float out;
  out=arcSin(c/(sqrt(1+c*c)));
  return out; // in radians
}


void setup() {
  //Pan Servo Setup
  panServo.attach(panPin);
  pinMode(panFeedBackPin, INPUT);
  
  //Tilt Servo Setup
  tiltServo.attach(tiltPin);
  tiltServo.writeMicroseconds(1500+tiltOffset);
  
  //Zoom Servo Setup
  zoomServo.attach(zoomPin);
 
     
  //Comunication Setup
  Serial.begin(9600);   //USB serial port
  Serial.println("hello world");
  Serial1.begin(19200); //HC12 wireless transceiver
 while(!Serial){       //Wait for USB serial port to connect 
 }
  
  //PID Setup
  panPID.SetMode(AUTOMATIC);
  panPID.SetOutputLimits(outputMin, outputMax);
  panPID.SetSampleTime(timeStep);
   
  //Timer
  millisStart=millis();
  panMillis=millis();
  targetAngleINCMillis=millis();
  panServoMillis=millis();
  
  //HMI Pins and LEDS
  pinMode(calPin1,INPUT);
  pinMode(calPin2,INPUT);
  pinMode(calPin3,INPUT);

  //Signal Processing
  memset(winLon,0,sizeof(winLon));//fill winLon with zeros
  memset(winLat,0,sizeof(winLat)); //fill winLat with zeros
  Serial.println("HELLO");
}

void loop() {
  recvSerialData(Serial1); 
  if(newData == true){
    parseGPSData();
    winLatAndLon(); //feed GPS data to regression buffer and Moving Average buffer
    newData = false;
  }    
  if(digitalRead(calPin4) == HIGH) {
	  maintenanceMode();
  }
  else if(digitalRead(calPin1)==HIGH) {
	  calibration();
  }
  else if(digitalRead(calPin3)==HIGH){   
    updateTranLoc();
  
    //Determine Pan Angle
    getPanAngle();

    //Update Target Angles
    if((newData == true) && (panAngle <= targetPanAngle+1) && (panAngle >= targetPanAngle-1 )) updateTargetPanAngle();
  
    //Pan Angle PID
    panPID.Compute();
    if(doublePanAngle>=(targetPanAngle-1) && doublePanAngle<=(targetPanAngle+1)) outputPanServo=1500;

    //Servo Control
    //panServo.writeMicroseconds((int)outputPanServo);
    
    //Display
    Serial.print(millis()-millisStart);  Serial.print(" ");
    Serial.print("targetPanAngle: "); Serial.print(targetPanAngle);  Serial.print(" ");
    Serial.print("panAngle: "); Serial.print(panAngle);  Serial.print(" ");
    Serial.print("panServoOutput: "); Serial.println(outputPanServo);
  
   //PID TESTING
  /* if((millis()-targetAngleINCMillis) >= 1000){
     targetPanAngle += 10;
     targetAngleINCMillis = millis();
     }
  */
  }//else if
  else{
    panServo.writeMicroseconds(1500);
  }
}//loop

