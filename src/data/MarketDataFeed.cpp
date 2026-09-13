// TODO: Story 2 (#19)
#include <iostream>
#include <stdexcept>

class Vector {
 private:
  double* elem;  // points to an array of doubles
  int sz;        // number of elements

 public:
  // Constructor
  Vector(int s);

  // Destructor
  ~Vector() { delete[] elem; }

  // Copy constructor
  Vector(const Vector& a);

  // Copy assignment operator
  Vector& operator=(const Vector& a);

  // Element access
  double& operator[](int i);
  const double& operator[](int i) const;

  // Size accessor
  int size() const { return sz; }
};

// Constructor
Vector::Vector(int s) : elem(new double[s]), sz(s) {
  for (int i = 0; i < sz; ++i)
    elem[i] = 0;
}

// Copy constructor
Vector::Vector(const Vector& a) : elem(new double[a.sz]), sz(a.sz) {
  for (int i = 0; i < sz; ++i)
    elem[i] = a.elem[i];
}

// Copy assignment operator
Vector& Vector::operator=(const Vector& a) {
  if (this == &a)
    return *this;

  double* p = new double[a.sz];

  for (int i = 0; i < a.sz; ++i)
    p[i] = a.elem[i];

  delete[] elem;

  elem = p;
  sz = a.sz;

  return *this;
}

// Element access
double& Vector::operator[](int i) {
  if (i < 0 || i >= sz)
    throw std::out_of_range("Vector index out of range");

  return elem[i];
}

// Const element access
const double& Vector::operator[](int i) const {
  if (i < 0 || i >= sz)
    throw std::out_of_range("Vector index out of range");

  return elem[i];
}

// Example usage
int main() {
  Vector v(3);

  v[0] = 1.1;
  v[1] = 2.2;
  v[2] = 3.3;

  Vector w = v;  // copy constructor

  for (int i = 0; i < w.size(); ++i)
    std::cout << w[i] << ' ';

  std::cout << '\n';

  return 0;
}