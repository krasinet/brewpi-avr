/*
 * simulator.h
 *
 * Created: 02/04/2013 18:01:55
 *  Author: mat
 */ 
#pragma once

#include "ExternalTempSensor.h"

/**
 * Thermal mass of air per unit volume, per degree.
 */
#define VOL_HC_AIR 0.00121			// J/cm^3/K

/**
 * Thermal mass of air, per unit mass, per degree.
 */
#define MASS_HC_AIR 1.012           // J/g/K

/**
 * Thermal mass of water, per unit mass, per degree.
 */
#define MASS_HC_WATER 4.18          // J/g/K

#include "limits.h"
#include "TempControl.h"

/**
 * Round a value to the nearest multiple of a quantity.
 */
inline double quantize(double value, double quantity) {
	return int((value+(quantity/2.0))/quantity)*quantity;
}

/**
 * Represents a heat potential as temperature x thermal mass per degree.
 */
struct HeatPotential
{
	double temp;
	double capacity;
	
	HeatPotential(double t1, double t2)	 {
		temp = t1; 
		capacity = t2;
	}			
};

/**
 * Some pointer types to make casting nicer.
 */
typedef ValueActuator*	PValueActuator;
typedef ValueSensor<bool>*	PValueSensor;

class Simulator
{
public:	
	Simulator(
				unsigned int _time=0, 
				unsigned int _fridgeVolume=400, double _fridgeTemp = 20.0, 
				double _beerSG=1.060, 
				double _beerTemp=22.0, double _beerVolume=20.0, 
				double _minRoomTemp = 13.0, double _maxRoomTemp = 18.0, 				
				double _heatPower = 25.0, double _coolPower = 50.0, 
				double _quantizeTempOutput=0.0625,
				double _coefficientChamberRoom = 1.67, double _coefficientChamberBeer = 3,
				double _sensorNoise = 0.0
	)
		: time(_time), fridgeVolume(_fridgeVolume), beerDensity(_beerSG), beerTemp(_beerTemp),
		beerVolume(_beerVolume), minRoomTemp(_minRoomTemp), maxRoomTemp(_maxRoomTemp), fridgeTemp(_fridgeTemp),
		heatPower(_heatPower), coolPower(_coolPower), quantizeTempOutput(_quantizeTempOutput),
		Ke(_coefficientChamberRoom), Kb(_coefficientChamberBeer), sensorNoise(_sensorNoise)
		{
			setBeerVolume(beerVolume);
			setFridgeVolume(fridgeVolume);
			updateSensors();
			time = 0;
			fermentPowerMax = 5;    // todo - rather max power, parameter should be ferment time, and compute power from moles of sugar
			heating = false;
			cooling = false;
			doorOpen = false;
		}

	struct TempPair
	{
		double t1;
		double t2;
	};

	void step() {
		// in the simulator, we assume that ValueActator instances are used for the actuators.		
		heating = PValueActuator(tempControl.heater)->isActive();
		cooling = PValueActuator(tempControl.cooler)->isActive();		 
		doorOpen = PValueSensor(tempControl.door)->sense();
#if 1	// with no serial and no calculation here we get 1500-2000x speedup
		// with this code enabled, around 1300x speedup
		// with serial, drops to 300x speedup
		double fermDiff = beerFerment();
		double heatingDiff = chamberHeating();
		double coolingDiff = chamberCooling();
		double doorDiff = doorLosses();

		double newBeerTemp = beerTemp + fermDiff;
		double newFridgeTemp = fridgeTemp + heatingDiff + coolingDiff + doorDiff;

		TempPair beerTx;
		TempPair roomTx;
		double currentRoomTemp = roomTemp();
		chamberBeerTransfer(fridgeTemp, beerTemp, beerTx);
		chamberRoomTransfer(fridgeTemp, currentRoomTemp, roomTx);

		newFridgeTemp += beerTx.t1 + roomTx.t1;		// transfer from beer and environment
		newBeerTemp += beerTx.t2;

		fridgeTemp = newFridgeTemp;
		beerTemp = newBeerTemp;
#endif		
		time += 1;
		
		updateSensors();
	}
	
	/**
	 * Set the beer temperature.
	 */
	void setBeerTemp(double beerTemp) {
		this->beerTemp = beerTemp;
	}
	
	double getBeerTemp() { return beerTemp; }

	double getBeerVolume() { return beerVolume; }
	
	void setMinRoomTemp(double minRoomTemp) { this->minRoomTemp = minRoomTemp; }
	double getMinRoomTemp() { return minRoomTemp; }
	void setMaxRoomTemp(double maxRoomTemp) { this->maxRoomTemp = maxRoomTemp; }
	double getMaxRoomTemp() { return maxRoomTemp; }
		
	void setFridgeTemp(double fridgeTemp) { this->fridgeTemp = fridgeTemp; }
	double getFridgeTemp() { return fridgeTemp; }
	void setHeatPower(int heatPowerInWatts) { this->heatPower = heatPowerInWatts; }
	int getHeatPower() { return this->heatPower; }
	void setCoolPower(int coolPowerInWatts) { this->coolPower = coolPowerInWatts; }
	int getCoolPower() { return this->coolPower; }
	
	double getQuantizeTemperatures() { return this->quantizeTempOutput; }
	void setQuantizeTemperatures(double interval) { this->quantizeTempOutput = interval; }
	
	double getRoomCoefficient() { return Ke; }
	double getBeerCoefficient() { return Kb; }
	void setRoomCoefficient(double e) { Ke = e; };
	void setBeerCoefficient(double e) { Kb = e; };
		
	void setFermentMaxPowerOutput(double max) { this->fermentPowerMax = max; }
	double getFermentMaxPowerOutput() { return fermentPowerMax; }

	void setFridgeVolume(unsigned int volumeInLiters)
	{
		fridgeVolume = volumeInLiters;
		fridgeHeatCapacity = fridgeVolume * 1000 * VOL_HC_AIR; // Heat capacity potential in J of the fridge per deg C.
		// assume a fridge made of steel with about 5kg of steel in the cabinet. Just a rough guess to provide some increased thermal mass.
		fridgeHeatCapacity += 2 /*kg*/ * 0.5 /* SHC steel */ * 1000 /* kJ -> J */;
	}

	double getFridgeVolume() { return fridgeVolume; }

	void setBeerDensity(double beerDensitySG) {
		this->beerDensity = beerDensitySG;
		updateBeerCapacity();
	}
	
	double getBeerDensity() { return beerDensity; }

	void setBeerVolume(double volumeInLiters)
	{
		beerVolume = volumeInLiters;
		updateBeerCapacity();
	}
	
	bool doorState() { return doorOpen; }

	void setConnected(TempSensor* sensor, bool connected) 
	{	
		ExternalTempSensor& externalSensor = (ExternalTempSensor&)sensor->basicTempSensor();
		externalSensor.setConnected(connected);
	}
	
	bool getConnected(TempSensor* sensor) {
		ExternalTempSensor& externalSensor = (ExternalTempSensor&)sensor->basicTempSensor();
		return externalSensor.isConnected();		
	}

	void setSwitch(Sensor<bool>* sensor, bool newSetting) {
		PValueSensor externalSensor = PValueSensor(sensor);
		externalSensor->setValue(newSetting);
	}
	
	void setSensorNoise(double noise) {
		this->sensorNoise = noise;
	}
	double getSensorNoise() {
		return sensorNoise;
	}

	double roomTemp()
	{
		if (minRoomTemp==maxRoomTemp)
			return minRoomTemp;
		
		unsigned long secondsInADay = 60L*60*24;
		double p = (double(time%secondsInADay)/double(secondsInADay))*(2.0*PI);
		double s = sin(p);
		double mid = (minRoomTemp+maxRoomTemp)/2;
		double half = mid-minRoomTemp;
		double r = mid + s*half;
		return r;
	}

private:	

	void updateSensors()
	{
		// add noise to the simulated temperature
		setTemp(tempControl.beerSensor, beerTemp+noise());
		setTemp(tempControl.fridgeSensor, fridgeTemp+noise());
	}

	void setTemp(TempSensor* sensor, double temp)
	{
		ExternalTempSensor& s = (ExternalTempSensor&)(sensor->basicTempSensor());
		fixed7_9 fixedTemp = temp>=INT_MAX ? INT_MAX*512L : temp<=INT_MIN ? INT_MIN*512L : temp*512L;
		s.setValue(fixedTemp);
	}
	
	double noise() {
		return sensorNoise==0.0 ? 0.0 : random(sensorNoise*1000.0)/1000.0;
	}


	void chamberBeerTransfer(double fridgeTemp, double beerTemp, TempPair& result)
	{		
		heatTransfer(HeatPotential(fridgeTemp, fridgeHeatCapacity), HeatPotential(beerTemp, beerHeatCapacity),
			Kb, result);
	}	

	void chamberRoomTransfer(double fridgeTemp, double roomTemp, TempPair& result)
	{
		heatTransfer(HeatPotential(fridgeTemp, fridgeHeatCapacity),
			HeatPotential(roomTemp, fridgeHeatCapacity), Ke, result);
	}

	/* Compute the heat transferred between two heat energy potentials. Returns the temperature change
			of each potential. */
	void heatTransfer(const HeatPotential& p1, const HeatPotential& p2, double k, TempPair& result)
	{
		double e1 = (p2.temp-p1.temp)*k;		// the energy transferred - from p2 to p1	
		result.t1 = e1/p1.capacity;				// change in temperature for the increase in energy
		result.t2 = -e1/p2.capacity;
	}

	double chamberHeating()
	{
		return heating ? (heatPower / fridgeHeatCapacity) : 0.0;
	}

	double chamberCooling()
	{
		return cooling ? -(coolPower / fridgeHeatCapacity) : 0.0;
	}
	
	double hours() { return time/3600.0; }

	double beerFerment() {
		double days = hours()/24.0;
		double scale = 0;
		if (days>5) 
			scale = 0;
		else if (days>2)
			scale = 1.0-((days-3)/3);
		else if (days>1)
			scale = 1.0;
		double power = scale*fermentPowerMax;
		return power / beerHeatCapacity;
	}

	double outputBeerTemp() {
		return outputTemp(beerTemp);
	}				

	double outputFridgeTemp() {
		return outputTemp(fridgeTemp);
	}

	double outputTemp(double temp) {
		return quantize(temp, quantizeTempOutput);
	}		

	double doorLosses(){
		return 0.0;
	}		
		
	void updateBeerCapacity()
	{
		beerHeatCapacity = beerVolume * beerDensity * 1000 * MASS_HC_WATER;             // Heat capacity potential in J of the beer per deg C.		
	}		

	
	unsigned long time;               // time since start of simulation in seconds
	int fridgeVolume;     // liters
	double beerDensity;    // SG
	double beerTemp;          // C
	double beerVolume;        // liters
	double minRoomTemp;       // C - 
	double maxRoomTemp;			
	double fridgeTemp;		  //
	unsigned int heatPower;         // W
	unsigned int coolPower;        // W
	double quantizeTempOutput;
	double Ke;              // W / K - thermal conductivity compartment <> environment
	double Kb;   // just a guess               // W / K  - thermal conductivity compartment <> beer
	double sensorNoise;          // how many quantization units of noise is generated
	
	/**
	 * When true, the heater is active.
	 */
	bool heating;
	/**
	 * When true, the cooler is active.
	 */
	bool cooling;
	
	/**
	 * When true, the door is open.
	 */
	bool doorOpen;
	
	/**
	 * Thermal mass of the fridge compartment. 
	 */		
	double fridgeHeatCapacity;
	/**
	 * Thermal mass of the beer compartment. 
	 */				
	double beerHeatCapacity;
	
	/**
	 * Maximum power produced by the exothermic fermentation. 
	 * This a bit of a hack - power should be derived from the quantity of sugar and number of yeast cells and 
	 * stipulated fermentation duration (e.g. 3 days, 10 days.)
	 */			
	double fermentPowerMax;		
};



// Work in progress
struct FermentPhases
{
	FermentPhases(double lagPhase=8, double logPhase=12, double activePhase=24, double stationaryPhase=48)
	{
		this->lagPhase = lagPhase;                    // 0 heat output
		this->logPhase = logPhase;                    // 0 -> max heat output, as more cells stop budding and start fermenting
		this->activePhase = activePhase;              // hold at max - yeast fermenting at max rate
		this->stationaryPhase = stationaryPhase;      // max -> 0  - prepare for stationary phase
	}
	
	double lagPhase;
	double logPhase;
	double activePhase;
	double stationaryPhase;
	// todo - better to model this as time to consume all sugars, and compute power output from quantity of sugar.
	
};


extern void setRunFactor(fixed7_9 factor);
extern Simulator simulator;

