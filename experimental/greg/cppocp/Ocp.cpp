// Ocp.cpp
// Greg Horn
// Casadi 2011

#include "Ocp.hpp"
#include <cstdlib>

#include <casadi/fx/sx_function.hpp>

using namespace std;
using namespace CasADi;

Ocp::Ocp(){}

Ocp::~Ocp()
{
	map<string,MultipleShooting*>::const_iterator msIter;
	for (msIter = ms.begin(); msIter != ms.end(); msIter++){
		delete msIter->second;
	}
}

void Ocp::addNonlconIneq( SXMatrix gNew, string name)
{
	if (gNew.size2() != 1){
		cerr << "gNew.size2() != 1" << endl;
		throw 1;
	}

	g = vertcat(g, gNew);
	gSizes.push_back(gNew.size1());
	gLabels.push_back(name);

	for (int k=0; k<gNew.size1(); k++){
		gMin.push_back(-1.0e50);
		gMax.push_back(0.0);
		//	-numeric_limits<double>::infinity()
	}
}
void Ocp::addNonlconIneq( SXMatrix gNew )
{
	addNonlconIneq(gNew, "");
}

void Ocp::addNonlconEq( SXMatrix gNew, string name)
{
	if (gNew.size2() != 1){
		cerr << "gNew.size2() != 1" << endl;
		throw 1;
	}

	g = vertcat(g, gNew);
	gSizes.push_back(gNew.size1());
	gLabels.push_back(name);

	for (int k=0; k<gNew.size1(); k++){
		gMin.push_back(0);
		gMax.push_back(0);
	}
}
void Ocp::addNonlconEq( SXMatrix gNew )
{
	addNonlconEq(gNew, "");
}

void Ocp::assertUniqueName(string s)
{
	if (isMultipleShooting(s) || isParam(s)) {
		cerr << "Error - new MultipleShooting or param \"" << s << "\" is not unique" << endl;
		throw "1";
	}

	// make sure name is not state/action in an ode
	map<string, MultipleShooting*>::const_iterator msIter;
	for (msIter = ms.begin(); msIter != ms.end(); msIter++)
		(msIter->second)->ode.assertUniqueName(s);
}


/****************** multiple shooting stuff ****************/
int Ocp::isMultipleShooting(string msName)
{
	map<string, MultipleShooting*>::const_iterator msIter;

	msIter = ms.find(msName);

	if (msIter != ms.end())
		return 1;
	return 0;
}

MultipleShooting & Ocp::addMultipleShooting(string name, Ode & ode, SX t0, SX tf, int N)
{
	assertUniqueName(name);

	int numNew = ode.nxu()*N;
	if (designVariables.size1() == 0)
		designVariables = SXMatrix(create_symbolic(name, numNew));
	else
		designVariables = vertcat( designVariables, SXMatrix(create_symbolic(name, numNew)) );

	for (int k=0; k<numNew; k++){
		lb.push_back(-1e50);
		ub.push_back(1e50);
		guess.push_back(0);
	}

	ms[name] = new MultipleShooting(name, ode, t0, tf, N, lb, ub, guess, designVariables, designVariables.size1() - ode.nxu()*N, params);

	// dynamics constraint
	for (int k=0; k<N-1; k++)
		addNonlconEq( ms[name]->getDynamicsConstraintError(k) );

	return *(ms[name]);
}


/****************** param stuff ******************/
// get param
SX & Ocp::getParam(string p)
{
	return params[p];
}

// add a param
SX & Ocp::addParam(string _newParam)
{
	assertUniqueName(_newParam);

	int idx = designVariables.size1();

	SXMatrix newDv = create_symbolic(_newParam, 1);
//	designVariables = vertcat( designVariables, SXMatrix(create_symbolic(_newParam, 1)) );
	if (designVariables.size1() == 0)
		designVariables = newDv;
	else
		designVariables = vertcat( designVariables, newDv );

	guess.push_back(0);
	lb.push_back(-1e50);
	ub.push_back(1e50);

	paramsIdx[_newParam] = idx;
	params[_newParam] = designVariables.at(idx);

	return getParam(_newParam);

}

void Ocp::boundParam(string p, double lb_, double ub_)
{
	if (!isParam(p)){
		cerr << "Error in Ocp::boundParam - \"" << p << "\" is not a known parameter\n";
		throw 1;
	}
	int idx = paramsIdx[p];
	lb[idx] = lb_;
	ub[idx] = ub_;
	if (guess[idx] < lb[idx]) guess[idx] = lb[idx];
	if (guess[idx] > ub[idx]) guess[idx] = ub[idx];
}



void Ocp::setParamGuess(string p, double guess_)
{
	int idx = paramsIdx[p];
	guess[idx] = guess_;
	if (guess[idx] < lb[idx] ){
		cerr << "Requested initial guess " << p << " == " << guess[idx];
		cerr << " is less than lb[" << idx << "] == " << lb[idx] << endl;
		cerr << "Setting guess " << p << " = " << lb[idx] << endl;
		guess[idx] = lb[idx];
	}
	if (guess[idx] > ub[idx] ){
		cerr << "Requested initial guess " << p << " == " << guess[idx];
		cerr << " is greater than ub[" << idx << "] == " << ub[idx] << endl;
		cerr << "Setting guess " << p << " = " << ub[idx] << endl;
		guess[idx] = ub[idx];
	}
}

int Ocp::isParam(string paramName)
{
	map<string, int>::const_iterator pIter;

	pIter = paramsIdx.find(paramName);

	if (pIter != paramsIdx.end())
		return 1;
	return 0;
}

void Ocp::writeSolution( const char * filename, double * xOpt )
{
	ofstream f(filename);

	if (!f){
		cerr << "error opening " << filename << endl;
		exit(1);
	}
	f.precision(15);
	for (int k=0; k<guess.size(); k++)
		f << xOpt[k] << endl;

	f.close();
}

void Ocp::loadGuess( const char * filename )
{
	ifstream f(filename);

	if (!f){
		cerr << "error opening " << filename << endl;
		exit(1);
	}

#define MAXLINE 200
	char oneline[MAXLINE];
	int k=0;
	while(f.getline(oneline, MAXLINE)){

		double value;
		if (1 == sscanf( oneline, "%lf", &value)){
			if (k >= guess.size()){
				cerr << "Too many design variables in \"" << filename << endl;
				f.close();
				throw -1;
			}

			guess[k] = value;
			//cout << "file[" << k << "]: " << value << ", guess.size: " << guess.size() << endl;
			k++;
		}
	}

	f.close();
}

void Ocp::writeOctaveOutput( string name, double * xOpt)
{
	string filename(name);
	filename.append(".m");
	ofstream f((char*)filename.c_str());

	if (!f){
		cerr << "error opening " << filename << endl;
		exit(1);
	}
	f.precision(10);

	f << "function opt = " << name << "()" << endl;
	f << "% autogenerated by Ocp::writeOctaveOutput\n\n";

	f << "opt = struct();\n\n";

	map<string, int>::const_iterator siIter;

	// params
	f << "% parameters\n";
	f << "opt.params = struct();\n";
	for (siIter = paramsIdx.begin(); siIter != paramsIdx.end(); siIter++){
		int idx = paramsIdx[ siIter->first ];
		f << "opt.params." << siIter->first << " = " << xOpt[idx] << ";" << endl;
	}
	f << endl;

	// multiple shootings
	map<string, MultipleShooting*>::const_iterator msIter;
	f << "% multiple shooting stages\n";
	f << "opt.multipleShootingStages = struct();\n";
	for (msIter = ms.begin(); msIter != ms.end(); msIter++){
		f << "opt.multipleShootingStages." << (msIter->second)->name << " = ms_stage_" << (msIter->second)->name << "_out();\n";
	}
	f << endl << endl;

	/************** tie multiple shootings stages together **************/
	f << "%%%%%%%%%% tie stages together in concatonated output %%%%%%%%%%%\n";

	// time
	f << "opt.time = [";
	for (msIter = ms.begin(); msIter != ms.end(); msIter++)
		f << "opt.multipleShootingStages." << (msIter->second)->name << ".time,";
	f << "];\n\n";
	
	map<string, MultipleShooting*>::const_iterator firstMs;
	firstMs = ms.begin();

	// states
	f << "% states\n";
	f << "opt.states = struct();\n";
	if (ms.size() > 0){

		// loop through states in first multiple shooting
		for (siIter = firstMs->second->ode.states.begin(); siIter != firstMs->second->ode.states.end(); siIter++){

			// for each state, concatonate all multiple shootings
			f << "try\n";
			f << "    opt.states." << siIter->first << " = [";
			for (msIter = ms.begin(); msIter != ms.end(); msIter++){
				f << "opt.multipleShootingStages." << msIter->first << ".states." << siIter->first << ",";
			}
			f << "];\n";
			f << "end\n";
		}
		f << endl << endl;
	}

	// actions
	f << "% actions\n";
	f << "opt.actions = struct();\n";
	if (ms.size() > 0){

		// loop through actions in first multiple shooting
		for (siIter = firstMs->second->ode.actions.begin(); siIter != firstMs->second->ode.actions.end(); siIter++){

			// for each state, concatonate all multiple shootings
			f << "try\n";
			f << "    opt.actions." << siIter->first << " = [";
			for (msIter = ms.begin(); msIter != ms.end(); msIter++){
				f << "opt.multipleShootingStages." << msIter->first << ".actions." << siIter->first << ",";
			}
			f << "];\n";
			f << "end\n";
		}
		f << endl << endl;
	}

	// outputs
	f << "% outputs\n";
	f << "opt.outputs = struct();\n";
	if (ms.size() > 0){

		// loop through outputs in first multiple shooting
		for (siIter = firstMs->second->ode.outputs.begin(); siIter != firstMs->second->ode.outputs.end(); siIter++){

			// for each state, concatonate all multiple shootings
			f << "try\n";
			f << "    opt.outputs." << siIter->first << " = [";
			for (msIter = ms.begin(); msIter != ms.end(); msIter++){
				f << "opt.multipleShootingStages." << msIter->first << ".outputs." << siIter->first << ",";
			}
			f << "];\n";
			f << "end\n";
		}
		f << endl << endl;
	}

	
	/************** write all the multiple shooting outputs *********/
	f << "%%%%%%%%%% each stage's individual output %%%%%%%%%%%\n";
	for (msIter = ms.begin(); msIter != ms.end(); msIter++){
		(msIter->second)->writeOctaveOutput( f, xOpt );
	}

	f.close();
}
